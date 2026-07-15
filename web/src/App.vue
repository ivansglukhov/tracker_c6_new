<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { BleClient } from './ble/BleClient'
import ConnectionHeader from './components/ConnectionHeader.vue'
import FiltersPage from './components/FiltersPage.vue'
import MapPage from './components/MapPage.vue'
import NowPage from './components/NowPage.vue'
import SettingsPage from './components/SettingsPage.vue'
import TracksPage from './components/TracksPage.vue'
import { appendTrackBlock, listStoredTracks, mergeRemoteTracks, readTrackFile } from './storage/trackDatabase'
import { parseC6t } from './track/c6t'
import { trackToGeoJson, trackToGpx } from './track/export'
import { filterTrack } from './track/filter'
import type { DeviceSettings, DeviceStatus, LivePoint, RemoteTrackInfo, StoredTrack, TrackFilterSettings, TrackPoint } from './types'

type Tab = 'now' | 'map' | 'tracks' | 'filters' | 'settings'

const client = new BleClient()
const connected = ref(false)
const busy = ref(false)
const connectionMessage = ref('Нет подключения')
const tab = ref<Tab>('now')
const status = ref<DeviceStatus>()
const point = ref<LivePoint>()
const error = ref('')
const settings = ref<DeviceSettings>({
  awakeTimeSec: 120,
  sleepTimeSec: 60,
  screenOnTimerWake: false,
  bleOnTimerWake: true,
  followSleepScheduleWhileBle: false,
})
const remoteTracks = ref<RemoteTrackInfo[]>([])
const storedTracks = ref<StoredTrack[]>([])
const busyTrackId = ref<number>()
const syncProgress = ref(0)
const history = ref<TrackPoint[]>([])
const filterSettings = ref<TrackFilterSettings>({ minSatellites: 0, maxHdop: 0, maxJumpM: 0 })

client.onConnection = (value) => {
  connected.value = value
  if (value) void refreshAfterConnection()
}
client.onConnectionState = (value) => { connectionMessage.value = value }
client.onStatus = (value) => { status.value = value }
client.onPoint = (value) => { point.value = value }

async function connect(): Promise<void> {
  busy.value = true
  error.value = ''
  try {
    await client.connect()
  } catch (reason) {
    error.value = reason instanceof Error ? reason.message : String(reason)
    if (!connected.value) connectionMessage.value = 'Нет подключения'
  } finally { busy.value = false }
}

async function refreshAfterConnection(): Promise<void> {
  try {
    settings.value = await client.getSettings()
    await refreshTracks()
  } catch (reason) {
    error.value = reason instanceof Error ? reason.message : String(reason)
  }
}

async function saveSettings(value: DeviceSettings): Promise<void> {
  try {
    await client.saveSettings(value)
    settings.value = value
  } catch (reason) { error.value = reason instanceof Error ? reason.message : String(reason) }
}

async function createTrack(): Promise<void> {
  if (!confirm('Закрыть текущий файл и создать новый трек?')) return
  try {
    await client.createTrack()
    await refreshTracks()
  }
  catch (reason) { error.value = reason instanceof Error ? reason.message : String(reason) }
}

async function refreshTracks(): Promise<void> {
  try {
    remoteTracks.value = await client.listTracks()
    storedTracks.value = (await mergeRemoteTracks(remoteTracks.value)).sort((a, b) => b.trackId - a.trackId)
  } catch (reason) {
    error.value = reason instanceof Error ? reason.message : String(reason)
  }
}

async function syncTrack(info: RemoteTrackInfo): Promise<void> {
  busyTrackId.value = info.trackId
  error.value = ''
  try {
    const local = storedTracks.value.find((track) => track.trackId === info.trackId)
    syncProgress.value = local?.bytesReceived ?? 0
    await client.downloadTrack(info, syncProgress.value, async (offset, data, fileSize) => {
      await appendTrackBlock(info.trackId, offset, data, fileSize)
      syncProgress.value = offset + data.byteLength
    })
    await refreshTracks()
  } catch (reason) {
    error.value = reason instanceof Error ? reason.message : String(reason)
    storedTracks.value = await listStoredTracks()
  } finally {
    busyTrackId.value = undefined
  }
}

async function viewTrack(id: number): Promise<void> {
  try {
    history.value = parseC6t(await readTrackFile(id)).points
    tab.value = 'map'
  } catch (reason) {
    error.value = reason instanceof Error ? reason.message : String(reason)
  }
}

async function exportTrackFile(id: number, format: 'gpx' | 'geojson'): Promise<void> {
  try {
    const points = filterTrack(parseC6t(await readTrackFile(id)).points, filterSettings.value)
    const basename = `track_${String(id).padStart(6, '0')}`
    const content = format === 'gpx' ? trackToGpx(points, basename) : trackToGeoJson(points, id)
    const blob = new Blob([content], { type: format === 'gpx' ? 'application/gpx+xml' : 'application/geo+json' })
    const url = URL.createObjectURL(blob)
    const link = document.createElement('a')
    link.href = url
    link.download = `${basename}.${format === 'gpx' ? 'gpx' : 'geojson'}`
    link.click()
    URL.revokeObjectURL(url)
  } catch (reason) {
    error.value = reason instanceof Error ? reason.message : String(reason)
  }
}

function saveFilter(value: TrackFilterSettings): void {
  filterSettings.value = value
  localStorage.setItem('c6-track-filter', JSON.stringify(value))
}

const tabs: { id: Tab; label: string }[] = [
  { id: 'now', label: 'Сейчас' },
  { id: 'map', label: 'Карта' },
  { id: 'tracks', label: 'Треки' },
  { id: 'filters', label: 'Фильтры' },
  { id: 'settings', label: 'Настройки' },
]

const trackId = computed(() => status.value?.trackId)
const filteredHistory = computed(() => filterTrack(history.value, filterSettings.value))

onMounted(async () => {
  storedTracks.value = await listStoredTracks()
  const cached = localStorage.getItem('c6-track-filter')
  if (cached) {
    try { filterSettings.value = { ...filterSettings.value, ...JSON.parse(cached) as TrackFilterSettings } }
    catch { localStorage.removeItem('c6-track-filter') }
  }
  void client.restoreKnownDevice().catch((reason) => {
    error.value = reason instanceof Error ? reason.message : String(reason)
  })
})
</script>

<template>
  <main class="app">
    <ConnectionHeader
      :connected="connected"
      :busy="busy"
      :status-text="connectionMessage"
      @connect="connect"
      @disconnect="client.disconnect()"
    />
    <nav class="tabs">
      <button v-for="item in tabs" :key="item.id" :class="{ active: tab === item.id }" @click="tab = item.id">{{ item.label }}</button>
    </nav>
    <p v-if="error" class="error">{{ error }}</p>
    <NowPage v-if="tab === 'now'" :status="status" :point="point" />
    <MapPage v-else-if="tab === 'map'" :point="point" :history="filteredHistory" />
    <TracksPage
      v-else-if="tab === 'tracks'"
      :connected="connected"
      :track-id="trackId"
      :point-count="status?.pointCount"
      :remote-tracks="remoteTracks"
      :stored-tracks="storedTracks"
      :busy-track-id="busyTrackId"
      :progress="syncProgress"
      @create="createTrack"
      @refresh="refreshTracks"
      @sync="syncTrack"
      @view="viewTrack"
      @export="exportTrackFile"
    />
    <FiltersPage v-else-if="tab === 'filters'" :settings="filterSettings" :raw-count="history.length" :filtered-count="filteredHistory.length" @save="saveFilter" />
    <SettingsPage v-else :settings="settings" :connected="connected" @save="saveSettings" />
  </main>
</template>
