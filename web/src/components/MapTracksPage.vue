<script setup lang="ts">
import L from 'leaflet'
import { computed, nextTick, onBeforeUnmount, onMounted, reactive, ref, watch } from 'vue'
import { distanceM, parseGuidance } from '../map/guidance'
import type { GuidanceRoute, LivePoint, RemoteTrackInfo, StoredTrack, TrackPoint } from '../types'

interface MapDisplaySettings {
  line: boolean
  points: boolean
  labels: boolean
  labelFields: Record<string, boolean>
}

const DISPLAY_KEY = 'c6-map-display-v2'
const props = defineProps<{
  point?: LivePoint
  history: TrackPoint[]
  connected: boolean
  trackId?: number
  pointCount?: number
  remoteTracks: RemoteTrackInfo[]
  storedTracks: StoredTrack[]
  busyTrackId?: number
  progress?: number
}>()

const emit = defineEmits<{
  create: []
  refresh: []
  sync: [track: RemoteTrackInfo]
  view: [trackId: number]
  export: [track: RemoteTrackInfo, format: 'gpx' | 'geojson']
}>()

const mapElement = ref<HTMLDivElement>()
const mapShell = ref<HTMLElement>()
const radiusM = ref(Number(localStorage.getItem('c6-guidance-radius') || 50))
const guidance = ref<GuidanceRoute | null>(null)
const activePoint = ref(0)
const message = ref('Загрузите GPX или GeoJSON для ведения.')
const selectedTrackId = ref(Number(localStorage.getItem('c6-map-track-id') || 0))
const fallbackFullscreen = ref(false)
const isFullscreen = ref(false)
const display = reactive<MapDisplaySettings>(loadDisplaySettings())
let map: L.Map | undefined
let guidanceLayer: L.LayerGroup | undefined
let trackLine: L.Polyline | undefined
let trackPointsLayer: L.LayerGroup | undefined
let liveMarker: L.CircleMarker | undefined

const displayedTracks = computed<RemoteTrackInfo[]>(() => {
  const tracks = new Map<number, RemoteTrackInfo>()
  props.storedTracks.forEach((track) => tracks.set(track.trackId, track))
  props.remoteTracks.forEach((track) => tracks.set(track.trackId, track))
  return [...tracks.values()].sort((a, b) => b.trackId - a.trackId)
})
const selectedTrack = computed(() => displayedTracks.value.find((track) => track.trackId === selectedTrackId.value))

function loadDisplaySettings(): MapDisplaySettings {
  const defaults: MapDisplaySettings = {
    line: true,
    points: false,
    labels: false,
    labelFields: {
      number: true,
      time: false,
      altitude: false,
      satellites: false,
      hdop: false,
      speed: false,
      course: false,
      distance: false,
      battery: false,
    },
  }
  try {
    const cached = JSON.parse(localStorage.getItem(DISPLAY_KEY) || 'null') as Partial<MapDisplaySettings> | null
    return cached ? { ...defaults, ...cached, labelFields: { ...defaults.labelFields, ...cached.labelFields } } : defaults
  } catch {
    localStorage.removeItem(DISPLAY_KEY)
    return defaults
  }
}

function pointLabel(point: TrackPoint, index: number): string {
  const values: string[] = []
  if (display.labelFields.number) values.push(`#${point.sequence || index + 1}`)
  if (display.labelFields.time && point.gpsEpoch) values.push(new Date(point.gpsEpoch * 1000).toLocaleTimeString('ru-RU'))
  if (display.labelFields.altitude) values.push(`${Math.round(point.altitudeM)} м`)
  if (display.labelFields.satellites) values.push(`спутники ${point.satellites}`)
  if (display.labelFields.hdop) values.push(`HDOP ${point.hdop}`)
  if (display.labelFields.speed) values.push(`${(point.speedMps * 3.6).toFixed(1)} км/ч`)
  if (display.labelFields.course) values.push(`курс ${point.courseDeg.toFixed(0)}°`)
  if (display.labelFields.distance) values.push(`${(point.cumulativeDistanceM / 1000).toFixed(2)} км`)
  if (display.labelFields.battery && point.batteryPercent != null) {
    values.push(`${point.batteryPercent}%${point.batteryMillivolts ? ` · ${(point.batteryMillivolts / 1000).toFixed(2)} В` : ''}`)
  }
  return values.join(' · ') || `#${index + 1}`
}

function renderTrack(fit = false): void {
  if (!map) return
  const coordinates = props.history.map((point) => [point.latitude, point.longitude] as L.LatLngTuple)
  trackLine?.remove()
  trackLine = undefined
  trackPointsLayer?.remove()
  trackPointsLayer = L.layerGroup().addTo(map)
  if (display.line && coordinates.length) trackLine = L.polyline(coordinates, { color: '#3c8cff', weight: 3 }).addTo(map)
  if (display.points) {
    const bounds = map.getBounds()
    const allowLabels = display.labels && map.getZoom() >= 13
    props.history.forEach((point, index) => {
      const coordinate: L.LatLngTuple = [point.latitude, point.longitude]
      const marker = L.circleMarker(coordinate, { radius: 3.5, weight: 1, color: '#8fc2ff', fillOpacity: 0.75 })
        .addTo(trackPointsLayer!)
      if (allowLabels && bounds.contains(coordinate)) {
        marker.bindTooltip(pointLabel(point, index), { permanent: true, direction: 'right', className: 'track-label' })
      } else {
        marker.bindPopup(pointLabel(point, index))
      }
    })
  }
  if (fit && coordinates.length) map.fitBounds(L.latLngBounds(coordinates), { padding: [24, 24] })
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

function applyLivePoint(point: LivePoint): void {
  if (!map) return
  const coordinate: L.LatLngTuple = [point.latitude, point.longitude]
  liveMarker?.remove()
  liveMarker = L.circleMarker(coordinate, { radius: 7, color: '#79dc7f', fillOpacity: 1 }).addTo(map)
  if (!props.history.length) map.setView(coordinate, 15)
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

function selectTrack(): void {
  localStorage.setItem('c6-map-track-id', String(selectedTrackId.value))
  if (selectedTrackId.value) emit('view', selectedTrackId.value)
}

function local(trackId: number): StoredTrack | undefined {
  return props.storedTracks.find((track) => track.trackId === trackId)
}

function size(value: number): string {
  return value < 1024 ? `${value} Б` : `${(value / 1024).toFixed(1)} КиБ`
}

async function toggleFullscreen(): Promise<void> {
  if (fallbackFullscreen.value) {
    fallbackFullscreen.value = false
  } else if (document.fullscreenElement) {
    await document.exitFullscreen()
  } else if (mapShell.value?.requestFullscreen) {
    try {
      await mapShell.value.requestFullscreen()
    } catch {
      fallbackFullscreen.value = true
    }
  } else {
    fallbackFullscreen.value = true
  }
  window.setTimeout(() => map?.invalidateSize(), 80)
}

function fullscreenChanged(): void {
  isFullscreen.value = Boolean(document.fullscreenElement)
  if (!document.fullscreenElement) fallbackFullscreen.value = false
  window.setTimeout(() => map?.invalidateSize(), 80)
}

watch(() => props.point, (point) => { if (point) applyLivePoint(point) })
watch(() => props.history, () => renderTrack(true), { deep: false })
watch(display, () => {
  localStorage.setItem(DISPLAY_KEY, JSON.stringify(display))
  renderTrack(false)
}, { deep: true })
watch(radiusM, (value) => localStorage.setItem('c6-guidance-radius', String(value)))

onMounted(async () => {
  await nextTick()
  map = L.map(mapElement.value!, { zoomControl: true }).setView([55.75, 37.62], 5)
  L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
    maxZoom: 19,
    attribution: '&copy; OpenStreetMap contributors',
  }).addTo(map)
  map.on('zoomend moveend', () => { if (display.points && display.labels) renderTrack(false) })
  document.addEventListener('fullscreenchange', fullscreenChanged)
  renderTrack(Boolean(props.history.length))
  const cached = localStorage.getItem('c6-guidance-route')
  if (cached) {
    try {
      guidance.value = JSON.parse(cached) as GuidanceRoute
      renderGuidance(false)
    } catch { localStorage.removeItem('c6-guidance-route') }
  }
  if (props.point) applyLivePoint(props.point)
})

onBeforeUnmount(() => {
  document.removeEventListener('fullscreenchange', fullscreenChanged)
  map?.remove()
})
</script>

<template>
  <section class="map-tracks-page">
    <section ref="mapShell" class="map-shell" :class="{ 'fallback-fullscreen': fallbackFullscreen }">
      <div class="map-overlay-tools">
        <button type="button" @click="toggleFullscreen">{{ isFullscreen || fallbackFullscreen ? 'Свернуть' : 'Во весь экран' }}</button>
      </div>
      <div ref="mapElement" class="map"></div>
    </section>

    <section class="panel map-display-settings">
      <h2>Отображение трека</h2>
      <label class="check"><input v-model="display.line" type="checkbox">Линия</label>
      <label class="check"><input v-model="display.points" type="checkbox">Точки</label>
      <label class="check"><input v-model="display.labels" type="checkbox" :disabled="!display.points">Подписи рядом с точками</label>
      <div v-if="display.points && display.labels" class="label-fields">
        <label v-for="(enabled, key) in display.labelFields" :key="key" class="check">
          <input v-model="display.labelFields[key]" type="checkbox">
          {{ ({ number: 'Номер', time: 'Время', altitude: 'Высота', satellites: 'Спутники', hdop: 'HDOP', speed: 'Скорость', course: 'Курс', distance: 'Расстояние', battery: 'Батарея' } as Record<string, string>)[key] }}
        </label>
      </div>
      <p v-if="display.labels" class="hint">Постоянные подписи появляются при масштабе 13 и крупнее. На меньшем масштабе данные открываются нажатием на точку.</p>
    </section>

    <section class="panel guidance-tools">
      <h2>Маршрут ведения</h2>
      <input type="file" accept=".gpx,.geojson,.json" @change="importRoute">
      <label>Радиус точки, м<input v-model.number="radiusM" type="number" min="1" max="1000"></label>
      <button :disabled="!guidance" @click="clearGuidance">Убрать маршрут</button>
      <span class="hint">{{ message }}</span>
    </section>

    <section class="panel track-picker">
      <h2>Треки на карте</h2>
      <label>Выбранный трек
        <select v-model.number="selectedTrackId" @change="selectTrack">
          <option :value="0">Не выбран</option>
          <option v-for="track in displayedTracks" :key="track.trackId" :value="track.trackId">
            track_{{ String(track.trackId).padStart(6, '0') }}.c6t{{ track.current ? ' · текущий' : '' }}
          </option>
        </select>
      </label>
      <p v-if="selectedTrack" class="hint">
        На телефоне: {{ size(local(selectedTrack.trackId)?.bytesReceived ?? 0) }} / {{ size(selectedTrack.fileSize) }}
      </p>
      <progress v-if="busyTrackId === selectedTrackId" :value="progress ?? 0" :max="selectedTrack?.fileSize ?? 1"></progress>
      <div class="actions">
        <button :disabled="!connected" @click="$emit('refresh')">Обновить список</button>
        <button class="primary" :disabled="!connected" @click="$emit('create')">Новый трек</button>
        <button :disabled="!selectedTrack || !connected || busyTrackId != null" @click="selectedTrack && $emit('sync', selectedTrack)">Синхронизировать</button>
        <button :disabled="!selectedTrack || busyTrackId != null" @click="selectedTrack && $emit('export', selectedTrack, 'gpx')">Скачать GPX</button>
        <button :disabled="!selectedTrack || busyTrackId != null" @click="selectedTrack && $emit('export', selectedTrack, 'geojson')">Скачать GeoJSON</button>
      </div>
      <p class="hint">Текущий файл: track_{{ String(trackId ?? 0).padStart(6, '0') }}.c6t · {{ pointCount ?? 0 }} GPS-точек.</p>
    </section>
  </section>
</template>
