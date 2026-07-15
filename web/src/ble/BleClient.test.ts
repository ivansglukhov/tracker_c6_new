import { afterEach, describe, expect, it, vi } from 'vitest'
import { UUID } from '../protocol/codec'
import { BleClient } from './BleClient'

type FakeCharacteristic = EventTarget & {
  value?: DataView
  startNotifications: ReturnType<typeof vi.fn>
  writeValueWithResponse: ReturnType<typeof vi.fn>
  writeValue: ReturnType<typeof vi.fn>
}

function characteristic(): FakeCharacteristic {
  const target = new EventTarget() as FakeCharacteristic
  target.startNotifications = vi.fn(async () => target)
  target.writeValueWithResponse = vi.fn(async () => undefined)
  target.writeValue = vi.fn(async () => undefined)
  return target
}

function trackerFixture(alreadyConnected = false) {
  const control = characteristic()
  const event = characteristic()
  const bulk = characteristic()
  const service = {
    getCharacteristic: vi.fn(async (uuid: string) => {
      if (uuid === UUID.control) return control
      if (uuid === UUID.event) return event
      if (uuid === UUID.bulk) return bulk
      throw new Error(`Unexpected characteristic ${uuid}`)
    }),
  }
  const device = new EventTarget() as BluetoothDevice
  const gatt = {
    connected: alreadyConnected,
    connect: vi.fn(async () => {
      gatt.connected = true
      return gatt
    }),
    disconnect: vi.fn(() => { gatt.connected = false }),
    getPrimaryService: vi.fn(async (uuid: string) => {
      if (uuid !== UUID.service) throw new Error(`Unexpected service ${uuid}`)
      return service
    }),
  }
  Object.defineProperties(device, {
    name: { value: 'C6 Tracker v2' },
    gatt: { value: gatt },
  })

  control.writeValueWithResponse = vi.fn(async (source: ArrayBufferView) => {
    const request = new DataView(source.buffer, source.byteOffset, source.byteLength)
    const response = new Uint8Array(6)
    const view = new DataView(response.buffer)
    view.setUint8(0, 1)
    view.setUint8(1, 0x7f)
    view.setUint16(2, request.getUint16(2, true), true)
    event.value = view
    event.dispatchEvent(new Event('characteristicvaluechanged'))
  })

  return { device, gatt, service, control, event, bulk }
}

function installBluetooth(options: {
  requestDevice?: () => Promise<BluetoothDevice>
  getDevices?: () => Promise<BluetoothDevice[]>
}) {
  const requestDevice = vi.fn(options.requestDevice)
  const getDevices = vi.fn(options.getDevices ?? (async () => []))
  vi.stubGlobal('window', globalThis)
  vi.stubGlobal('navigator', { bluetooth: { requestDevice, getDevices } })
  return { requestDevice, getDevices }
}

afterEach(() => {
  vi.useRealTimers()
  vi.unstubAllGlobals()
})

describe('BleClient Android connection flow', () => {
  it('uses the permissive chooser and grants access to the custom service', async () => {
    const fixture = trackerFixture()
    const bluetooth = installBluetooth({ requestDevice: async () => fixture.device })
    const client = new BleClient()

    await client.connect()

    expect(bluetooth.requestDevice).toHaveBeenCalledWith({
      acceptAllDevices: true,
      optionalServices: [UUID.service],
    })
    expect(fixture.control.writeValueWithResponse).toHaveBeenCalledOnce()
    expect(fixture.control.writeValue).not.toHaveBeenCalled()
  })

  it('restores a previously approved tracker without opening the chooser', async () => {
    const fixture = trackerFixture()
    const bluetooth = installBluetooth({
      requestDevice: async () => fixture.device,
      getDevices: async () => [fixture.device],
    })
    const client = new BleClient()

    await expect(client.restoreKnownDevice()).resolves.toBe(true)

    expect(bluetooth.requestDevice).not.toHaveBeenCalled()
    expect(fixture.gatt.connect).toHaveBeenCalledOnce()
  })

  it('finishes service discovery when Android already considers GATT connected', async () => {
    const fixture = trackerFixture(true)
    installBluetooth({ requestDevice: async () => fixture.device })
    const client = new BleClient()

    await client.connect()

    expect(fixture.gatt.connect).not.toHaveBeenCalled()
    expect(fixture.gatt.getPrimaryService).toHaveBeenCalledWith(UUID.service)
    expect(fixture.event.startNotifications).toHaveBeenCalledOnce()
    expect(fixture.bulk.startNotifications).toHaveBeenCalledOnce()
  })
})
