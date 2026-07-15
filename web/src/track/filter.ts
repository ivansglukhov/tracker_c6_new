import type { TrackFilterSettings, TrackPoint } from '../types'

function distanceM(a: TrackPoint, b: TrackPoint): number {
  const radians = Math.PI / 180
  const lat1 = a.latitude * radians
  const lat2 = b.latitude * radians
  const deltaLat = lat2 - lat1
  const deltaLon = (b.longitude - a.longitude) * radians
  const value = Math.sin(deltaLat / 2) ** 2 +
    Math.cos(lat1) * Math.cos(lat2) * Math.sin(deltaLon / 2) ** 2
  return 6_371_000 * 2 * Math.atan2(Math.sqrt(value), Math.sqrt(1 - value))
}

export function filterTrack(points: TrackPoint[], settings: TrackFilterSettings): TrackPoint[] {
  const output: TrackPoint[] = []
  for (const point of points) {
    if (settings.minSatellites > 0 && point.satellites < settings.minSatellites) continue
    if (settings.maxHdop > 0 && (point.hdop <= 0 || point.hdop > settings.maxHdop)) continue
    const previous = output.at(-1)
    if (previous && settings.maxJumpM > 0 && distanceM(previous, point) > settings.maxJumpM) continue
    output.push(point)
  }
  return output
}
