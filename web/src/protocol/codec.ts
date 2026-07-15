import type { DeviceSettings, DeviceStatus, LivePoint, RemoteTrackInfo } from '../types'
import { crc32 } from './crc32'

export const UUID = {
  service: '6f4c2000-b9a8-4e31-9d7a-c6c6c6c62000',
  control: '6f4c2001-b9a8-4e31-9d7a-c6c6c6c62000',
  event: '6f4c2002-b9a8-4e31-9d7a-c6c6c6c62000',
  bulk: '6f4c2003-b9a8-4e31-9d7a-c6c6c6c62000',
} as const

export const Opcode = {
  getStatus: 0x01,
  getSettings: 0x02,
  setSettings: 0x03,
  createTrack: 0x10,
  listTracks: 0x11,
  openTransfer: 0x20,
  ackTransfer: 0x21,
  cancelTransfer: 0x22,
  statusEvent: 0x40,
  livePointEvent: 0x41,
  bulkChunk: 0x42,
  response: 0x7f,
} as const

export interface OpenTransferInfo {
  sessionId: number
  trackId: number
  fileSize: number
  nextOffset: number
}

export interface BulkChunk {
  sessionId: number
  trackId: number
  offset: number
  data: Uint8Array
}

export function requestFrame(opcode: number, requestId: number, payload?: Uint8Array): Uint8Array {
  const frame = new Uint8Array(4 + (payload?.byteLength ?? 0))
  const view = new DataView(frame.buffer)
  view.setUint8(0, 1)
  view.setUint8(1, opcode)
  view.setUint16(2, requestId, true)
  if (payload) frame.set(payload, 4)
  return frame
}

export function settingsPayload(settings: DeviceSettings): Uint8Array {
  const payload = new Uint8Array(7)
  const view = new DataView(payload.buffer)
  view.setUint16(0, settings.awakeTimeSec, true)
  view.setUint32(2, settings.sleepTimeSec, true)
  let flags = 0
  if (settings.screenOnTimerWake) flags |= 0x01
  if (settings.bleOnTimerWake) flags |= 0x02
  if (settings.followSleepScheduleWhileBle) flags |= 0x04
  view.setUint8(6, flags)
  return payload
}

export function uint32Payload(value: number): Uint8Array {
  const payload = new Uint8Array(4)
  new DataView(payload.buffer).setUint32(0, value, true)
  return payload
}

export function openTransferPayload(trackId: number, offset: number): Uint8Array {
  const payload = new Uint8Array(8)
  const view = new DataView(payload.buffer)
  view.setUint32(0, trackId, true)
  view.setUint32(4, offset, true)
  return payload
}

export function transferOffsetPayload(sessionId: number, offset: number): Uint8Array {
  const payload = new Uint8Array(8)
  const view = new DataView(payload.buffer)
  view.setUint16(0, sessionId, true)
  view.setUint32(4, offset, true)
  return payload
}

export function cancelTransferPayload(sessionId: number): Uint8Array {
  const payload = new Uint8Array(2)
  new DataView(payload.buffer).setUint16(0, sessionId, true)
  return payload
}

export function decodeTrackInfo(payload: Uint8Array): RemoteTrackInfo | null {
  if (payload.byteLength !== 16) throw new Error('Некорректный пакет описания трека')
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
  const trackId = view.getUint32(0, true)
  if (trackId === 0) return null
  return {
    trackId,
    fileSize: view.getUint32(4, true),
    createdEpoch: view.getUint32(8, true),
    current: Boolean(view.getUint32(12, true) & 1),
  }
}

export function decodeOpenTransfer(payload: Uint8Array): OpenTransferInfo {
  if (payload.byteLength !== 16) throw new Error('Некорректный ответ открытия передачи')
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
  return {
    sessionId: view.getUint16(0, true),
    trackId: view.getUint32(4, true),
    fileSize: view.getUint32(8, true),
    nextOffset: view.getUint32(12, true),
  }
}

export function decodeBulkChunk(value: DataView): BulkChunk {
  if (value.byteLength < 22 || value.getUint8(0) !== 1 || value.getUint8(1) !== Opcode.bulkChunk) {
    throw new Error('Некорректный заголовок блока трека')
  }
  const encoding = value.getUint8(12)
  const payloadLength = value.getUint16(14, true)
  const rawLength = value.getUint16(16, true)
  if (value.byteLength !== 22 + payloadLength || rawLength === 0) throw new Error('Некорректная длина блока трека')
  const payload = new Uint8Array(value.buffer, value.byteOffset + 22, payloadLength)
  const data = encoding === 0 ? new Uint8Array(payload) : encoding === 1 ? unpackBits(payload, rawLength) : undefined
  if (!data || data.byteLength !== rawLength) throw new Error('Некорректное сжатие блока трека')
  if (crc32(data) !== value.getUint32(18, true)) throw new Error('Ошибка CRC блока трека')
  return {
    sessionId: value.getUint16(2, true),
    trackId: value.getUint32(4, true),
    offset: value.getUint32(8, true),
    data,
  }
}

function unpackBits(payload: Uint8Array, expectedLength: number): Uint8Array {
  const output = new Uint8Array(expectedLength)
  let source = 0
  let target = 0
  while (source < payload.byteLength) {
    const control = payload[source++]
    if (control & 0x80) {
      const length = (control & 0x7f) + 3
      if (source >= payload.byteLength || target + length > output.byteLength) throw new Error('Повреждённый PackBits-блок')
      output.fill(payload[source++], target, target + length)
      target += length
    } else {
      const length = control + 1
      if (source + length > payload.byteLength || target + length > output.byteLength) throw new Error('Повреждённый PackBits-блок')
      output.set(payload.subarray(source, source + length), target)
      source += length
      target += length
    }
  }
  if (target !== output.byteLength) throw new Error('Неполный PackBits-блок')
  return output
}

export function decodeSettings(payload: Uint8Array): DeviceSettings {
  if (payload.byteLength !== 7) throw new Error('Некорректный пакет настроек')
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
  const flags = view.getUint8(6)
  return {
    awakeTimeSec: view.getUint16(0, true),
    sleepTimeSec: view.getUint32(2, true),
    screenOnTimerWake: Boolean(flags & 0x01),
    bleOnTimerWake: Boolean(flags & 0x02),
    followSleepScheduleWhileBle: Boolean(flags & 0x04),
  }
}

export function decodeStatus(value: DataView): DeviceStatus {
  if (value.byteLength !== 32 || value.getUint8(0) !== 1 || value.getUint8(1) !== Opcode.statusEvent) {
    throw new Error('Некорректный пакет статуса')
  }
  return {
    flags: value.getUint16(2, true),
    trackId: value.getUint32(4, true),
    pointCount: value.getUint32(8, true),
    distanceM: value.getUint32(12, true),
    altitudeM: value.getInt32(16, true),
    awakeElapsedSec: value.getUint16(20, true),
    awakeTimeSec: value.getUint16(22, true),
    interactiveRemainingSec: value.getUint16(24, true),
    batteryPercent: value.getUint8(26),
    satellites: value.getUint8(27),
    wakeReason: value.getUint8(28),
    batteryMillivolts: value.getUint16(30, true),
  }
}

export function decodeLivePoint(value: DataView): LivePoint {
  if (value.byteLength !== 28 || value.getUint8(0) !== 1 || value.getUint8(1) !== Opcode.livePointEvent) {
    throw new Error('Некорректный пакет GPS-точки')
  }
  return {
    trackId: value.getUint32(2, true),
    sampleId: value.getUint32(6, true),
    gpsEpoch: value.getUint32(10, true),
    latitude: value.getInt32(14, true) / 10_000_000,
    longitude: value.getInt32(18, true) / 10_000_000,
    altitudeM: value.getInt32(22, true) / 100,
    satellites: value.getUint8(26),
    flags: value.getUint8(27),
  }
}
