import { describe, expect, it } from 'vitest'
import type { TrackPoint } from '../types'
import { filterTrack } from './filter'

const base: TrackPoint = {
  sequence: 1,
  gpsEpoch: 1,
  latitude: 55,
  longitude: 37,
  altitudeM: 0,
  speedMps: 0,
  courseDeg: 0,
  hdop: 1,
  satellites: 8,
  flags: 1,
  wakeCycleId: 1,
  cumulativeDistanceM: 0,
}

describe('браузерная фильтрация', () => {
  it('ничего не удаляет при отключённых порогах', () => {
    const points = [base, { ...base, sequence: 2, satellites: 0, hdop: 99 }]
    expect(filterTrack(points, { minSatellites: 0, maxHdop: 0, maxJumpM: 0 })).toEqual(points)
  })

  it('отбрасывает слабую и скачкообразную точку, не меняя исходный массив', () => {
    const weak = { ...base, sequence: 2, satellites: 3 }
    const jump = { ...base, sequence: 3, latitude: 56 }
    const points = [base, weak, jump]
    const filtered = filterTrack(points, { minSatellites: 5, maxHdop: 2, maxJumpM: 1000 })
    expect(filtered.map((point) => point.sequence)).toEqual([1])
    expect(points).toHaveLength(3)
  })
})
