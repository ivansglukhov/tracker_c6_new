import { describe, expect, it } from 'vitest'
import type { TrackPoint } from '../types'
import { trackToGeoJson, trackToGpx } from './export'

const point: TrackPoint = {
  sequence: 1,
  gpsEpoch: 1_800_000_000,
  latitude: 55.7558,
  longitude: 37.6173,
  altitudeM: 123.45,
  speedMps: 0,
  courseDeg: 0,
  hdop: 1.2,
  satellites: 9,
  flags: 7,
  wakeCycleId: 1,
  cumulativeDistanceM: 0,
  batteryMillivolts: 3912,
  batteryPercent: 84,
}

describe('экспорт полного представления', () => {
  it('создаёт GPX 1.1', () => {
    const gpx = trackToGpx([point], 'track_000001')
    expect(gpx).toContain('<trkpt lat="55.7558" lon="37.6173">')
    expect(gpx).toContain('<sat>9</sat>')
    expect(gpx).toContain('<c6:batteryPercent>84</c6:batteryPercent>')
    expect(gpx).toContain('<c6:speedMps>0</c6:speedMps>')
  })

  it('создаёт GeoJSON LineString', () => {
    const value = JSON.parse(trackToGeoJson([point], 1))
    expect(value.geometry).toEqual({ type: 'LineString', coordinates: [[37.6173, 55.7558, 123.45]] })
    expect(value.properties.points[0]).toMatchObject({
      satellites: 9,
      batteryPercent: 84,
      batteryMillivolts: 3912,
      wakeCycleId: 1,
    })
  })
})
