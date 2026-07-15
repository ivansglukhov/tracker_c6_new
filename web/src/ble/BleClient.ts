import {
  Opcode,
  UUID,
  cancelTransferPayload,
  decodeBulkChunk,
  decodeLivePoint,
  decodeOpenTransfer,
  decodeSettings,
  decodeStatus,
  decodeTrackInfo,
  openTransferPayload,
  requestFrame,
  settingsPayload,
  transferOffsetPayload,
  uint32Payload,
} from '../protocol/codec'
import type { DeviceSettings, DeviceStatus, LivePoint, RemoteTrackInfo } from '../types'

interface PendingRequest {
  resolve: (payload: Uint8Array) => void
  reject: (error: Error) => void
  timeout: number
}

interface DownloadState {
  sessionId: number
  trackId: number
  fileSize: number
  nextOffset: number
  processing: boolean
  onChunk: (offset: number, data: Uint8Array, fileSize: number) => Promise<void>
  resolve: () => void
  reject: (error: Error) => void
}

export class BleClient {
  private device?: BluetoothDevice
  private control?: BluetoothRemoteGATTCharacteristic
  private event?: BluetoothRemoteGATTCharacteristic
  private bulk?: BluetoothRemoteGATTCharacteristic
  private nextRequestId = 1
  private pending = new Map<number, PendingRequest>()
  private download?: DownloadState
  private autoReconnect = false
  private reconnectTimer?: number
  private connecting = false
  private gattQueue: Promise<void> = Promise.resolve()

  onStatus?: (status: DeviceStatus) => void
  onPoint?: (point: LivePoint) => void
  onConnection?: (connected: boolean) => void
  onConnectionState?: (message: string) => void

  async connect(): Promise<void> {
    if (!navigator.bluetooth) throw new Error('Web Bluetooth недоступен в этом браузере')
    this.onConnectionState?.('Выберите C6 Tracker v2 в списке устройств')
    const device = await navigator.bluetooth.requestDevice({
      acceptAllDevices: true,
      optionalServices: [UUID.service],
    })
    this.useDevice(device)
    try {
      await this.connectKnownDevice()
    } catch (error) {
      this.scheduleReconnect()
      throw error
    }
  }

  async restoreKnownDevice(): Promise<boolean> {
    if (!this.autoReconnect || !navigator.bluetooth?.getDevices) return false
    const devices = await navigator.bluetooth.getDevices()
    const known = devices.find((candidate) => candidate.name === 'C6 Tracker v2')
      ?? devices.find((candidate) => candidate.name?.startsWith('C6 Tracker'))
    if (!known) return false

    this.useDevice(known)
    this.onConnectionState?.('Ожидание C6 Tracker v2…')
    try {
      await this.connectKnownDevice()
      return true
    } catch {
      this.scheduleReconnect()
      return false
    }
  }

  private async connectKnownDevice(): Promise<void> {
    if (!this.device || this.connecting) return
    if (this.device.gatt?.connected && this.control && this.event && this.bulk) return
    this.connecting = true
    try {
      this.clearCharacteristics()
      this.onConnectionState?.(`Подключение к ${this.device.name || 'трекеру'}…`)
      const gatt = this.device.gatt
      if (!gatt) throw new Error('Устройство не предоставляет GATT')
      const server = gatt.connected ? gatt : await gatt.connect()
      const service = await server.getPrimaryService(UUID.service)
      const control = await service.getCharacteristic(UUID.control)
      const event = await service.getCharacteristic(UUID.event)
      const bulk = await service.getCharacteristic(UUID.bulk)
      event.addEventListener('characteristicvaluechanged', this.handleEvent)
      bulk.addEventListener('characteristicvaluechanged', this.handleBulk)
      await event.startNotifications()
      await bulk.startNotifications()
      this.control = control
      this.event = event
      this.bulk = bulk
      this.onConnectionState?.('Проверка двоичного протокола…')
      await this.getStatus()
      this.onConnection?.(true)
      this.onConnectionState?.(`Подключено: ${this.device.name || 'C6 Tracker v2'}`)
    } catch (error) {
      this.clearCharacteristics()
      if (this.device.gatt?.connected) this.device.gatt.disconnect()
      this.onConnection?.(false)
      throw error
    } finally {
      this.connecting = false
    }
  }

  disconnect(): void {
    this.autoReconnect = false
    if (this.reconnectTimer != null) window.clearTimeout(this.reconnectTimer)
    this.reconnectTimer = undefined
    this.onConnectionState?.('Отключено пользователем')
    if (this.device?.gatt?.connected) this.device.gatt.disconnect()
    else this.handleDisconnect()
  }

  setAutoReconnect(enabled: boolean): void {
    this.autoReconnect = enabled
    if (!enabled && this.reconnectTimer != null) {
      window.clearTimeout(this.reconnectTimer)
      this.reconnectTimer = undefined
    } else if (enabled && this.device && !this.device.gatt?.connected) {
      this.scheduleReconnect()
    }
  }

  async getStatus(): Promise<void> {
    await this.request(Opcode.getStatus)
  }

  async getSettings(): Promise<DeviceSettings> {
    return decodeSettings(await this.request(Opcode.getSettings))
  }

  async saveSettings(settings: DeviceSettings): Promise<void> {
    await this.request(Opcode.setSettings, settingsPayload(settings))
  }

  async createTrack(): Promise<void> {
    await this.request(Opcode.createTrack)
  }

  async listTracks(): Promise<RemoteTrackInfo[]> {
    const tracks: RemoteTrackInfo[] = []
    let afterTrackId = 0
    for (let index = 0; index < 100_000; index++) {
      const info = decodeTrackInfo(await this.request(Opcode.listTracks, uint32Payload(afterTrackId)))
      if (!info) return tracks
      if (info.trackId <= afterTrackId) throw new Error('Трекер нарушил порядок списка файлов')
      tracks.push(info)
      afterTrackId = info.trackId
    }
    throw new Error('Слишком много файлов треков')
  }

  async downloadTrack(
    info: RemoteTrackInfo,
    offset: number,
    onChunk: (offset: number, data: Uint8Array, fileSize: number) => Promise<void>,
  ): Promise<void> {
    if (this.download) throw new Error('Уже выполняется другая передача')
    if (offset < 0 || offset > info.fileSize) throw new Error('Некорректное смещение продолжения')
    const opened = decodeOpenTransfer(await this.request(
      Opcode.openTransfer,
      openTransferPayload(info.trackId, offset),
    ))
    if (opened.trackId !== info.trackId || opened.nextOffset !== offset || opened.fileSize < offset) {
      throw new Error('Трекер вернул несогласованные параметры передачи')
    }
    if (opened.nextOffset === opened.fileSize) return

    return new Promise<void>((resolve, reject) => {
      this.download = {
        sessionId: opened.sessionId,
        trackId: opened.trackId,
        fileSize: opened.fileSize,
        nextOffset: opened.nextOffset,
        processing: false,
        onChunk,
        resolve,
        reject,
      }
    })
  }

  private request(opcode: number, payload?: Uint8Array): Promise<Uint8Array> {
    if (!this.control) return Promise.reject(new Error('Трекер не подключён'))
    const requestId = this.nextRequestId++ & 0xffff || 1
    const frame = requestFrame(opcode, requestId, payload)
    const writeBuffer = new Uint8Array(frame.byteLength)
    writeBuffer.set(frame)
    return new Promise<Uint8Array>((resolve, reject) => {
      const timeout = window.setTimeout(() => {
        this.pending.delete(requestId)
        reject(new Error('Таймаут ответа трекера'))
      }, 5000)
      this.pending.set(requestId, { resolve, reject, timeout })
      this.writeControl(writeBuffer).catch((error) => {
        window.clearTimeout(timeout)
        this.pending.delete(requestId)
        reject(error instanceof Error ? error : new Error(String(error)))
      })
    })
  }

  private handleEvent = (event: Event): void => {
    const characteristic = event.target as BluetoothRemoteGATTCharacteristic
    const value = characteristic.value
    if (!value || value.byteLength < 2) return
    const opcode = value.getUint8(1)
    if (opcode === Opcode.statusEvent) this.onStatus?.(decodeStatus(value))
    else if (opcode === Opcode.livePointEvent) this.onPoint?.(decodeLivePoint(value))
    else if (opcode === Opcode.response && value.byteLength >= 6) {
      const requestId = value.getUint16(2, true)
      const result = value.getUint8(4)
      const size = value.getUint8(5)
      const pending = this.pending.get(requestId)
      if (!pending) return
      window.clearTimeout(pending.timeout)
      this.pending.delete(requestId)
      if (result !== 0) pending.reject(new Error(`Трекер вернул ошибку ${result}`))
      else pending.resolve(new Uint8Array(value.buffer, value.byteOffset + 6, Math.min(size, value.byteLength - 6)))
    }
  }

  private handleBulk = (event: Event): void => {
    const characteristic = event.target as BluetoothRemoteGATTCharacteristic
    if (characteristic.value) void this.processBulk(characteristic.value)
  }

  private async processBulk(value: DataView): Promise<void> {
    const transfer = this.download
    if (!transfer || transfer.processing) return
    transfer.processing = true
    try {
      const chunk = decodeBulkChunk(value)
      if (chunk.sessionId !== transfer.sessionId || chunk.trackId !== transfer.trackId) return
      if (chunk.offset !== transfer.nextOffset || chunk.data.byteLength === 0 ||
          chunk.offset + chunk.data.byteLength > transfer.fileSize) {
        throw new Error('Нарушена последовательность блоков трека')
      }
      await transfer.onChunk(chunk.offset, chunk.data, transfer.fileSize)
      transfer.nextOffset += chunk.data.byteLength
      await this.request(Opcode.ackTransfer, transferOffsetPayload(transfer.sessionId, transfer.nextOffset))
      if (transfer.nextOffset >= transfer.fileSize) {
        this.download = undefined
        transfer.resolve()
      }
    } catch (reason) {
      const error = reason instanceof Error ? reason : new Error(String(reason))
      this.download = undefined
      transfer.reject(error)
      void this.request(Opcode.cancelTransfer, cancelTransferPayload(transfer.sessionId)).catch(() => undefined)
    } finally {
      transfer.processing = false
    }
  }

  private handleDisconnect = (): void => {
    this.clearCharacteristics()
    this.gattQueue = Promise.resolve()
    for (const request of this.pending.values()) {
      window.clearTimeout(request.timeout)
      request.reject(new Error('Bluetooth отключён'))
    }
    this.pending.clear()
    if (this.download) {
      this.download.reject(new Error('Передача прервана: Bluetooth отключён'))
      this.download = undefined
    }
    this.onConnection?.(false)
    if (this.autoReconnect) this.onConnectionState?.('Ожидание пробуждения трекера…')
    this.scheduleReconnect()
  }

  private scheduleReconnect(): void {
    if (!this.autoReconnect || !this.device || this.reconnectTimer != null) return
    this.reconnectTimer = window.setTimeout(async () => {
      this.reconnectTimer = undefined
      try {
        await this.connectKnownDevice()
      } catch {
        this.onConnectionState?.('Трекер недоступен, следующая попытка через 3 с')
        this.scheduleReconnect()
      }
    }, 3000)
  }

  private useDevice(device: BluetoothDevice): void {
    if (this.device && this.device !== device) {
      this.device.removeEventListener('gattserverdisconnected', this.handleDisconnect)
    }
    this.device = device
    this.device.removeEventListener('gattserverdisconnected', this.handleDisconnect)
    this.device.addEventListener('gattserverdisconnected', this.handleDisconnect)
  }

  private clearCharacteristics(): void {
    this.event?.removeEventListener('characteristicvaluechanged', this.handleEvent)
    this.bulk?.removeEventListener('characteristicvaluechanged', this.handleBulk)
    this.control = undefined
    this.event = undefined
    this.bulk = undefined
  }

  private writeControl(value: Uint8Array): Promise<void> {
    const operation = this.gattQueue.then(async () => {
      const control = this.control
      if (!control) throw new Error('Трекер не подключён')
      const payload = new Uint8Array(value)
      if (control.writeValueWithResponse) await control.writeValueWithResponse(payload)
      else await control.writeValue(payload)
    })
    this.gattQueue = operation.catch(() => undefined)
    return operation
  }
}
