<script setup lang="ts">
import L from 'leaflet'
import { nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { distanceM, parseGuidance } from '../map/guidance'
import type { GuidanceRoute, LivePoint, TrackPoint } from '../types'

const props = defineProps<{ point?: LivePoint; history: TrackPoint[] }>()
const mapElement = ref<HTMLDivElement>()
const radiusM = ref(Number(localStorage.getItem('c6-guidance-radius') || 50))
const guidance = ref<GuidanceRoute | null>(null)
const activePoint = ref(0)
const message = ref('Загрузите GPX или GeoJSON.')
let map: L.Map | undefined
let guidanceLayer: L.LayerGroup | undefined
let liveLine: L.Polyline | undefined
const liveCoordinates: L.LatLngExpression[] = []

function renderHistory(): void {
  if (!map) return
  const coordinates = props.history.map((point) => [point.latitude, point.longitude] as L.LatLngTuple)
  liveCoordinates.splice(0, liveCoordinates.length, ...coordinates)
  if (!liveLine) liveLine = L.polyline(liveCoordinates, { color: '#3c8cff', weight: 3 }).addTo(map)
  else liveLine.setLatLngs(liveCoordinates)
  if (coordinates.length) map.fitBounds(L.latLngBounds(coordinates), { padding: [24, 24] })
}

function renderGuidance(fit = true): void {
  if (!map) return
  guidanceLayer?.remove()
  guidanceLayer = L.layerGroup().addTo(map)
  if (!guidance.value?.points.length) return
  const coordinates = guidance.value.points.map((point) => [point.latitude, point.longitude] as L.LatLngTuple)
  if (guidance.value.type === 'line') L.polyline(coordinates, { color: '#e45151', weight: 3 }).addTo(guidanceLayer)
  else guidance.value.points.forEach((point, index) => {
    L.circleMarker([point.latitude, point.longitude], {
      radius: index === activePoint.value ? 8 : 5,
      color: index === activePoint.value ? '#f4d35e' : '#e45151',
      fillOpacity: 0.9,
    }).bindTooltip(point.name || `${index + 1}`).addTo(guidanceLayer!)
  })
  if (fit) map.fitBounds(L.latLngBounds(coordinates), { padding: [24, 24] })
}

async function importRoute(event: Event): Promise<void> {
  const input = event.target as HTMLInputElement
  const file = input.files?.[0]
  if (!file) return
  try {
    guidance.value = parseGuidance(await file.text(), file.name)
    activePoint.value = 0
    localStorage.setItem('c6-guidance-route', JSON.stringify(guidance.value))
    message.value = guidance.value.type === 'points'
      ? `Точечный маршрут: ${guidance.value.points.length} точек.`
      : `Линейный маршрут: ${guidance.value.points.length} узлов.`
    renderGuidance()
  } catch (error) {
    message.value = error instanceof Error ? error.message : String(error)
  } finally {
    input.value = ''
  }
}

function clearGuidance(): void {
  guidance.value = null
  activePoint.value = 0
  guidanceLayer?.clearLayers()
  localStorage.removeItem('c6-guidance-route')
  message.value = 'Маршрут ведения убран. Записанный трек не изменён.'
}

function applyPoint(point: LivePoint): void {
  if (!map) return
  const coordinate: L.LatLngTuple = [point.latitude, point.longitude]
  liveCoordinates.push(coordinate)
  if (!liveLine) liveLine = L.polyline(liveCoordinates, { color: '#3c8cff', weight: 3 }).addTo(map)
  else liveLine.setLatLngs(liveCoordinates)
  if (liveCoordinates.length === 1) map.setView(coordinate, 15)

  if (guidance.value?.type === 'points' && activePoint.value < guidance.value.points.length) {
    const target = guidance.value.points[activePoint.value]
    const remaining = distanceM({ latitude: point.latitude, longitude: point.longitude }, target)
    if (remaining <= radiusM.value) {
      activePoint.value++
      renderGuidance(false)
    }
    message.value = activePoint.value >= guidance.value.points.length
      ? 'Все контрольные точки посещены.'
      : `Следующая точка: ${activePoint.value + 1}/${guidance.value.points.length}, ${Math.round(remaining)} м.`
  }
}

watch(() => props.point, (point) => { if (point) applyPoint(point) })
watch(() => props.history, () => renderHistory(), { deep: false })
watch(radiusM, (value) => localStorage.setItem('c6-guidance-radius', String(value)))

onMounted(async () => {
  await nextTick()
  map = L.map(mapElement.value!).setView([55.75, 37.62], 5)
  L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
    maxZoom: 19,
    attribution: '&copy; OpenStreetMap contributors',
  }).addTo(map)
  renderHistory()
  const cached = localStorage.getItem('c6-guidance-route')
  if (cached) {
    try {
      guidance.value = JSON.parse(cached) as GuidanceRoute
      renderGuidance()
    } catch { localStorage.removeItem('c6-guidance-route') }
  }
  if (props.point) applyPoint(props.point)
})

onBeforeUnmount(() => map?.remove())
</script>

<template>
  <section class="map-page">
    <div class="map-tools panel">
      <input type="file" accept=".gpx,.geojson,.json" @change="importRoute">
      <label>Радиус точки, м<input v-model.number="radiusM" type="number" min="1" max="1000"></label>
      <button :disabled="!guidance" @click="clearGuidance">Убрать маршрут</button>
      <span class="hint">{{ message }}</span>
    </div>
    <div ref="mapElement" class="map"></div>
  </section>
</template>
