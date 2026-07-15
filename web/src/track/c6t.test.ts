import { describe, expect, it } from 'vitest'
import { crc32 } from '../protocol/crc32'
import { parseC6t } from './c6t'

function setCrc(bytes: Uint8Array, bodyLength: number, crcOffset: number): void {
  new DataView(bytes.buffer).setUint32(crcOffset, crc32(bytes.subarray(0, bodyLength)), true)
}

describe('формат C6T2', () => {
  it('читает заголовок и точку с проверкой CRC', () => {
    const bytes = new Uint8Array(88)
    const header = new DataView(bytes.buffer, 0, 40)
    header.setUint32(0, 0x32543643, true)
    header.setUint16(4, 1, true)
    header.setUint16(6, 40, true)
    header.setUint32(8, 12, true)
    header.setBigUint64(12, 0x1234n, true)
    header.setUint32(20, 1_800_000_000, true)
    header.setUint32(24, 120, true)
    header.setUint32(28, 60, true)
    setCrc(bytes.subarray(0, 40), 36, 36)

    const recordBytes = bytes.subarray(40)
    const record = new DataView(recordBytes.buffer, recordBytes.byteOffset, 48)
    record.setUint16(0, 0xc62a, true)
    record.setUint8(2, 1)
    record.setUint8(3, 1)
    record.setUint32(4, 1, true)
    record.setUint32(8, 1_800_000_001, true)
    record.setInt32(12, 557_558_000, true)
    record.setInt32(16, 376_173_000, true)
    record.setInt32(20, 12_345, true)
    record.setUint16(24, 250, true)
    record.setUint16(26, 9_000, true)
    record.setUint16(28, 123, true)
    record.setUint8(30, 9)
    record.setUint8(31, 7)
    record.setUint32(32, 3, true)
    record.setUint32(36, 456, true)
    record.setUint32(40, (0xb1 << 24) | (84 << 16) | 3912, true)
    record.setUint32(44, crc32(recordBytes.subarray(0, 44)), true)

    const parsed = parseC6t(bytes)
    expect(parsed.trackId).toBe(12)
    expect(parsed.points).toHaveLength(1)
    expect(parsed.points[0]).toMatchObject({
      sequence: 1,
      latitude: 55.7558,
      longitude: 37.6173,
      hdop: 1.23,
      cumulativeDistanceM: 456,
      batteryMillivolts: 3912,
      batteryPercent: 84,
    })
  })

  it('читает отдельную запись батареи без GPS-координаты', () => {
    const bytes = new Uint8Array(88)
    const header = new DataView(bytes.buffer, 0, 40)
    header.setUint32(0, 0x32543643, true)
    header.setUint16(4, 1, true)
    header.setUint16(6, 40, true)
    header.setUint32(8, 7, true)
    setCrc(bytes.subarray(0, 40), 36, 36)

    const recordBytes = bytes.subarray(40)
    const record = new DataView(recordBytes.buffer, recordBytes.byteOffset, 48)
    record.setUint16(0, 0xc62a, true)
    record.setUint8(2, 1)
    record.setUint8(3, 3)
    record.setUint32(8, 1_800_000_010, true)
    record.setUint32(32, 9, true)
    record.setUint32(40, (0xb1 << 24) | (78 << 16) | 3820, true)
    record.setUint32(44, crc32(recordBytes.subarray(0, 44)), true)

    const parsed = parseC6t(bytes)
    expect(parsed.points).toHaveLength(0)
    expect(parsed.batterySamples).toEqual([{
      gpsEpoch: 1_800_000_010,
      wakeCycleId: 9,
      batteryMillivolts: 3820,
      batteryPercent: 78,
    }])
  })
})
