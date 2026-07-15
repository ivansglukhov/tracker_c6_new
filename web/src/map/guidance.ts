import type { GuidancePoint, GuidanceRoute } from '../types'

function numericAttribute(element: Element, name: string): number {
  const value = Number(element.getAttribute(name))
  if (!Number.isFinite(value)) throw new Error(`Некорректная координата ${name}`)
  return value
}

function gpxPoints(elements: Element[]): GuidancePoint[] {
  return elements.map((element, index) => ({
    latitude: numericAttribute(element, 'lat'),
    longitude: numericAttribute(element, 'lon'),
    name: element.querySelector('name')?.textContent?.trim() || `${index + 1}`,
  }))
}

function geoJsonPoints(value: unknown, output: GuidancePoint[]): void {
  if (!value || typeof value !== 'object') return
  const object = value as Record<string, unknown>
  if (object.type === 'FeatureCollection' && Array.isArray(object.features)) {
    for (const feature of object.features) geoJsonPoints(feature, output)
  } else if (object.type === 'Feature') {
    geoJsonPoints(object.geometry, output)
  } else if (object.type === 'Point' && Array.isArray(object.coordinates)) {
    const [longitude, latitude] = object.coordinates.map(Number)
    if (Number.isFinite(latitude) && Number.isFinite(longitude)) output.push({ latitude, longitude })
  } else if (object.type === 'MultiPoint' && Array.isArray(object.coordinates)) {
    for (const coordinate of object.coordinates) geoJsonPoints({ type: 'Point', coordinates: coordinate }, output)
  }
}

export function parseGuidance(text: string, filename: string): GuidanceRoute {
  if (/\.gpx$/i.test(filename) || text.trimStart().startsWith('<')) {
    const document = new DOMParser().parseFromString(text, 'application/xml')
    if (document.querySelector('parsererror')) throw new Error('Не удалось прочитать GPX')
    const track = Array.from(document.querySelectorAll('trkpt'))
    if (track.length) return { name: filename, type: 'line', points: gpxPoints(track) }
    const route = Array.from(document.querySelectorAll('rtept'))
    if (route.length) return { name: filename, type: 'points', points: gpxPoints(route) }
    const waypoints = Array.from(document.querySelectorAll('wpt'))
    if (waypoints.length) return { name: filename, type: 'points', points: gpxPoints(waypoints) }
    throw new Error('В GPX нет trkpt, rtept или wpt')
  }

  const value = JSON.parse(text) as Record<string, unknown>
  if (value.type === 'LineString' && Array.isArray(value.coordinates)) {
    const points = value.coordinates.map((coordinate) => {
      const [longitude, latitude] = (coordinate as unknown[]).map(Number)
      return { latitude, longitude }
    })
    return { name: filename, type: 'line', points }
  }
  if (value.type === 'Feature' && (value.geometry as Record<string, unknown> | undefined)?.type === 'LineString') {
    return parseGuidance(JSON.stringify(value.geometry), filename)
  }
  const points: GuidancePoint[] = []
  geoJsonPoints(value, points)
  if (!points.length) throw new Error('В GeoJSON нет линии или точек')
  return { name: filename, type: 'points', points }
}

export function distanceM(a: GuidancePoint, b: GuidancePoint): number {
  const radians = Math.PI / 180
  const lat1 = a.latitude * radians
  const lat2 = b.latitude * radians
  const dLat = lat2 - lat1
  const dLon = (b.longitude - a.longitude) * radians
  const value = Math.sin(dLat / 2) ** 2 + Math.cos(lat1) * Math.cos(lat2) * Math.sin(dLon / 2) ** 2
  return 6_371_000 * 2 * Math.atan2(Math.sqrt(value), Math.sqrt(1 - value))
}

