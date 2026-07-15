import type { TrackPoint } from '../types'

function isoTime(epoch: number): string {
  return new Date(epoch * 1000).toISOString()
}

export function trackToGpx(points: TrackPoint[], name: string): string {
  const body = points.map((point) => {
    const time = point.gpsEpoch ? `<time>${isoTime(point.gpsEpoch)}</time>` : ''
    return `<trkpt lat="${point.latitude}" lon="${point.longitude}"><ele>${point.altitudeM}</ele>${time}<sat>${point.satellites}</sat><hdop>${point.hdop}</hdop></trkpt>`
  }).join('')
  return `<?xml version="1.0" encoding="UTF-8"?><gpx version="1.1" creator="C6 Tracker v2" xmlns="http://www.topografix.com/GPX/1/1"><trk><name>${name}</name><trkseg>${body}</trkseg></trk></gpx>`
}

export function trackToGeoJson(points: TrackPoint[], trackId: number): string {
  return JSON.stringify({
    type: 'Feature',
    properties: {
      trackId,
      pointCount: points.length,
      gpsEpoch: points.map((point) => point.gpsEpoch),
      satellites: points.map((point) => point.satellites),
      hdop: points.map((point) => point.hdop),
    },
    geometry: {
      type: 'LineString',
      coordinates: points.map((point) => [point.longitude, point.latitude, point.altitudeM]),
    },
  })
}
