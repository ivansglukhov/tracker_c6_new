import { crc32 } from '../protocol/crc32'
import type { TrackPoint } from '../types'

const FILE_MAGIC = 0x32543643
const RECORD_MAGIC = 0xc62a
const VERSION = 1
const HEADER_SIZE = 40
const RECORD_SIZE = 48

export interface ParsedTrack {
  trackId: number
  deviceId: bigint
  createdEpoch: number
  awakeTimeSec: number
  sleepTimeSec: number
  points: TrackPoint[]
  closed: boolean
}

function checkedCrc(bytes: Uint8Array, bodyLength: number, expected: number, description: string): void {
  if (crc32(bytes.subarray(0, bodyLength)) !== expected) throw new Error(`Ошибка CRC: ${description}`)
}

export function parseC6t(bytes: Uint8Array): ParsedTrack {
  if (bytes.byteLength < HEADER_SIZE) throw new Error('Файл короче заголовка C6T2')
  const header = new DataView(bytes.buffer, bytes.byteOffset, HEADER_SIZE)
  if (header.getUint32(0, true) !== FILE_MAGIC || header.getUint16(4, true) !== VERSION ||
      header.getUint16(6, true) !== HEADER_SIZE) throw new Error('Неподдерживаемый заголовок C6T2')
  checkedCrc(bytes.subarray(0, HEADER_SIZE), 36, header.getUint32(36, true), 'заголовок')
  if ((bytes.byteLength - HEADER_SIZE) % RECORD_SIZE !== 0) throw new Error('Неполная запись в хвосте файла')

  const points: TrackPoint[] = []
  let closed = false
  for (let offset = HEADER_SIZE; offset < bytes.byteLength; offset += RECORD_SIZE) {
    const recordBytes = bytes.subarray(offset, offset + RECORD_SIZE)
    const record = new DataView(recordBytes.buffer, recordBytes.byteOffset, RECORD_SIZE)
    if (record.getUint16(0, true) !== RECORD_MAGIC || record.getUint8(2) !== VERSION) {
      throw new Error(`Некорректная запись по смещению ${offset}`)
    }
    checkedCrc(recordBytes, 44, record.getUint32(44, true), `запись ${offset}`)
    const type = record.getUint8(3)
    if (type === 2) {
      closed = true
      continue
    }
    if (type !== 1) throw new Error(`Неизвестный тип записи ${type}`)
    points.push({
      sequence: record.getUint32(4, true),
      gpsEpoch: record.getUint32(8, true),
      latitude: record.getInt32(12, true) / 10_000_000,
      longitude: record.getInt32(16, true) / 10_000_000,
      altitudeM: record.getInt32(20, true) / 100,
      speedMps: record.getUint16(24, true) / 100,
      courseDeg: record.getUint16(26, true) / 100,
      hdop: record.getUint16(28, true) / 100,
      satellites: record.getUint8(30),
      flags: record.getUint8(31),
      wakeCycleId: record.getUint32(32, true),
      cumulativeDistanceM: record.getUint32(36, true),
    })
  }

  return {
    trackId: header.getUint32(8, true),
    deviceId: header.getBigUint64(12, true),
    createdEpoch: header.getUint32(20, true),
    awakeTimeSec: header.getUint32(24, true),
    sleepTimeSec: header.getUint32(28, true),
    points,
    closed,
  }
}
