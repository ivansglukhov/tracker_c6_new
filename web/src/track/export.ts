import type { TrackPoint } from '../types'

function isoTime(epoch: number): string {
  return new Date(epoch * 1000).toISOString()
}

function xml(value: string): string {
  return value.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;').replaceAll("'", '&apos;')
}

export function trackToGpx(points: TrackPoint[], name: string): string {
  const body = points.map((point) => {
    const time = point.gpsEpoch ? `<time>${isoTime(point.gpsEpoch)}</time>` : ''
    const battery = point.batteryPercent == null ? ''
      : `<c6:batteryPercent>${point.batteryPercent}</c6:batteryPercent><c6:batteryMillivolts>${point.batteryMillivolts ?? 0}</c6:batteryMillivolts>`
    return `<trkpt lat="${point.latitude}" lon="${point.longitude}"><ele>${point.altitudeM}</ele>${time}<sat>${point.satellites}</sat><hdop>${point.hdop}</hdop><extensions><c6:sequence>${point.sequence}</c6:sequence><c6:gpsEpoch>${point.gpsEpoch}</c6:gpsEpoch><c6:speedMps>${point.speedMps}</c6:speedMps><c6:courseDeg>${point.courseDeg}</c6:courseDeg><c6:flags>${point.flags}</c6:flags><c6:wakeCycleId>${point.wakeCycleId}</c6:wakeCycleId><c6:cumulativeDistanceM>${point.cumulativeDistanceM}</c6:cumulativeDistanceM>${battery}</extensions></trkpt>`
  }).join('')
  return `<?xml version="1.0" encoding="UTF-8"?><gpx version="1.1" creator="C6 Tracker v2" xmlns="http://www.topografix.com/GPX/1/1" xmlns:c6="https://github.com/ivansglukhov/tracker_c6_new/ns/1"><trk><name>${xml(name)}</name><trkseg>${body}</trkseg></trk></gpx>`
}

export function trackToGeoJson(points: TrackPoint[], trackId: number): string {
  return JSON.stringify({
    type: 'Feature',
    properties: {
      trackId,
      pointCount: points.length,
      points: points.map((point) => ({
        sequence: point.sequence,
        gpsEpoch: point.gpsEpoch,
        time: point.gpsEpoch ? isoTime(point.gpsEpoch) : null,
        altitudeM: point.altitudeM,
        speedMps: point.speedMps,
        courseDeg: point.courseDeg,
        hdop: point.hdop,
        satellites: point.satellites,
        flags: point.flags,
        wakeCycleId: point.wakeCycleId,
        cumulativeDistanceM: point.cumulativeDistanceM,
        batteryMillivolts: point.batteryMillivolts ?? null,
        batteryPercent: point.batteryPercent ?? null,
      })),
    },
    geometry: {
      type: 'LineString',
      coordinates: points.map((point) => [point.longitude, point.latitude, point.altitudeM]),
    },
  })
}
