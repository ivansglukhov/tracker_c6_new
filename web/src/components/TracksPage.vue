<script setup lang="ts">
import { computed } from 'vue'
import type { RemoteTrackInfo, StoredTrack } from '../types'

const props = defineProps<{
  connected: boolean
  trackId?: number
  pointCount?: number
  remoteTracks: RemoteTrackInfo[]
  storedTracks: StoredTrack[]
  busyTrackId?: number
  progress?: number
}>()

defineEmits<{
  create: []
  refresh: []
  sync: [track: RemoteTrackInfo]
  view: [trackId: number]
  export: [track: RemoteTrackInfo, format: 'gpx' | 'geojson']
}>()

const displayedTracks = computed<RemoteTrackInfo[]>(() => {
  const tracks = new Map<number, RemoteTrackInfo>()
  props.storedTracks.forEach((track) => tracks.set(track.trackId, track))
  props.remoteTracks.forEach((track) => tracks.set(track.trackId, track))
  return [...tracks.values()].sort((a, b) => b.trackId - a.trackId)
})

const currentTrack = computed(() => displayedTracks.value.find((track) => track.trackId === props.trackId)
  ?? displayedTracks.value.find((track) => track.current))

function local(trackId: number): StoredTrack | undefined {
  return props.storedTracks.find((track) => track.trackId === trackId)
}

function size(value: number): string {
  return value < 1024 ? `${value} Б` : `${(value / 1024).toFixed(1)} КиБ`
}

function complete(trackId: number): boolean {
  const stored = local(trackId)
  return Boolean(stored && stored.bytesReceived === stored.fileSize)
}

function availableRemotely(trackId: number): boolean {
  return props.remoteTracks.some((track) => track.trackId === trackId)
}

function canExport(track: RemoteTrackInfo): boolean {
  return complete(track.trackId) || (props.connected && availableRemotely(track.trackId))
}
</script>

<template>
  <section class="tracks-page">
    <div class="panel">
      <h2>Текущий файл</h2>
      <p>track_{{ String(trackId ?? 0).padStart(6, '0') }}.c6t · {{ pointCount ?? 0 }} точек</p>
      <div class="actions">
        <button class="primary" :disabled="!connected" @click="$emit('create')">Новый трек</button>
        <button :disabled="!connected" @click="$emit('refresh')">Обновить список</button>
        <button :disabled="!currentTrack || !canExport(currentTrack) || busyTrackId != null" @click="currentTrack && $emit('export', currentTrack, 'gpx')">Скачать текущий GPX</button>
        <button :disabled="!currentTrack || !canExport(currentTrack) || busyTrackId != null" @click="currentTrack && $emit('export', currentTrack, 'geojson')">Скачать текущий GeoJSON</button>
      </div>
    </div>

    <div class="panel">
      <h2>Треки</h2>
      <p v-if="!displayedTracks.length" class="empty">Подключите трекер и обновите список.</p>
      <div v-else class="track-list">
        <article v-for="track in displayedTracks" :key="track.trackId" class="track-row">
          <div>
            <strong>track_{{ String(track.trackId).padStart(6, '0') }}.c6t</strong>
            <span>{{ size(local(track.trackId)?.bytesReceived ?? 0) }} / {{ size(track.fileSize) }}<template v-if="track.current"> · текущий</template></span>
          </div>
          <progress v-if="busyTrackId === track.trackId" :value="progress ?? 0" :max="track.fileSize"></progress>
          <div class="actions">
            <button :disabled="!connected || !availableRemotely(track.trackId) || busyTrackId != null" @click="$emit('sync', track)">
              {{ local(track.trackId)?.bytesReceived === track.fileSize ? 'Проверить обновления' : 'Синхронизировать' }}
            </button>
            <button :disabled="!complete(track.trackId)" @click="$emit('view', track.trackId)">На карту</button>
            <button :disabled="!canExport(track) || busyTrackId != null" @click="$emit('export', track, 'gpx')">Скачать GPX</button>
            <button :disabled="!canExport(track) || busyTrackId != null" @click="$emit('export', track, 'geojson')">Скачать GeoJSON</button>
          </div>
        </article>
      </div>
    </div>
  </section>
</template>
