import { describe, expect, it } from 'vitest'
import { decodeBulkChunk, decodeLivePoint, decodeNmeaGga, decodeSettings, decodeStatus, Opcode, settingsPayload } from './codec'
import { crc32 } from './crc32'

describe('двоичный протокол BLE', () => {
  it('кодирует и декодирует настройки без потери полей', () => {
    const source = {
      awakeTimeSec: 120,
      sleepTimeSec: 3600,
      pointsBeforeSleep: 3,
      screenOnTimerWake: false,
      followSleepScheduleWhileBle: true,
    }

    const bytes = settingsPayload(source)

    expect(bytes).toHaveLength(9)
    expect(decodeSettings(bytes)).toEqual(source)
  })

  it('декодирует 36-байтовый статус по смещениям прошивки', () => {
    const bytes = new Uint8Array(36)
    const view = new DataView(bytes.buffer)
    view.setUint8(0, 1)
    view.setUint8(1, Opcode.statusEvent)
    view.setUint16(2, 0x001f, true)
    view.setUint32(4, 42, true)
    view.setUint32(8, 1234, true)
    view.setUint32(12, 9876, true)
    view.setInt32(16, 2115, true)
    view.setUint16(20, 17, true)
    view.setUint16(22, 120, true)
    view.setUint16(24, 13, true)
    view.setUint8(26, 84)
    view.setUint8(27, 9)
    view.setUint8(28, 2)
    view.setUint16(30, 3912, true)
    view.setUint16(32, 2, true)
    view.setUint16(34, 3, true)

    expect(decodeStatus(view)).toEqual({
      flags: 0x001f,
      trackId: 42,
      pointCount: 1234,
      distanceM: 9876,
      altitudeM: 2115,
      awakeElapsedSec: 17,
      awakeTimeSec: 120,
      interactiveRemainingSec: 13,
      cyclePointCount: 2,
      pointsBeforeSleep: 3,
      batteryPercent: 84,
      batteryMillivolts: 3912,
      satellites: 9,
      wakeReason: 2,
    })
  })

  it('декодирует сервисную строку NMEA GGA', () => {
    const sentence = '$GNGGA,123519,,,,,0,00,99.99,,,,,,*48'
    const body = new TextEncoder().encode(sentence)
    const bytes = new Uint8Array(body.length + 2)
    bytes[0] = 1
    bytes[1] = Opcode.nmeaGgaEvent
    bytes.set(body, 2)
    expect(decodeNmeaGga(new DataView(bytes.buffer))).toBe(sentence)
  })

  it('декодирует 28-байтовую GPS-точку', () => {
    const bytes = new Uint8Array(28)
    const view = new DataView(bytes.buffer)
    view.setUint8(0, 1)
    view.setUint8(1, Opcode.livePointEvent)
    view.setUint32(2, 7, true)
    view.setUint32(6, 15, true)
    view.setUint32(10, 1_800_000_000, true)
    view.setInt32(14, 557_558_000, true)
    view.setInt32(18, 376_173_000, true)
    view.setInt32(22, 12_345, true)
    view.setUint8(26, 11)
    view.setUint8(27, 7)

    expect(decodeLivePoint(view)).toEqual({
      trackId: 7,
      sampleId: 15,
      gpsEpoch: 1_800_000_000,
      latitude: 55.7558,
      longitude: 37.6173,
      altitudeM: 123.45,
      satellites: 11,
      flags: 7,
    })
  })

  it('совпадает с CRC32 прошивки и проверяет bulk-блок', () => {
    const sample = new TextEncoder().encode('123456789')
    expect(crc32(sample)).toBe(0xcbf43926)

    const bytes = new Uint8Array(22 + sample.byteLength)
    const view = new DataView(bytes.buffer)
    view.setUint8(0, 1)
    view.setUint8(1, Opcode.bulkChunk)
    view.setUint16(2, 9, true)
    view.setUint32(4, 4, true)
    view.setUint32(8, 360, true)
    view.setUint8(12, 0)
    view.setUint16(14, sample.byteLength, true)
    view.setUint16(16, sample.byteLength, true)
    view.setUint32(18, crc32(sample), true)
    bytes.set(sample, 22)

    expect(decodeBulkChunk(view)).toEqual({
      sessionId: 9,
      trackId: 4,
      offset: 360,
      data: sample,
    })
  })

  it('распаковывает PackBits bulk-блок', () => {
    const raw = new Uint8Array([0, 0, 0, 0, 1, 2, 3])
    const packed = new Uint8Array([0x81, 0, 0x02, 1, 2, 3])
    const bytes = new Uint8Array(22 + packed.byteLength)
    const view = new DataView(bytes.buffer)
    view.setUint8(0, 1)
    view.setUint8(1, Opcode.bulkChunk)
    view.setUint16(2, 2, true)
    view.setUint32(4, 1, true)
    view.setUint32(8, 0, true)
    view.setUint8(12, 1)
    view.setUint16(14, packed.byteLength, true)
    view.setUint16(16, raw.byteLength, true)
    view.setUint32(18, crc32(raw), true)
    bytes.set(packed, 22)
    expect(decodeBulkChunk(view).data).toEqual(raw)
  })
})
