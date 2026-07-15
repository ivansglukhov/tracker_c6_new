<script setup lang="ts">
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
  export: [trackId: number, format: 'gpx' | 'geojson']
}>()

function local(trackId: number): StoredTrack | undefined {
  return props.storedTracks.find((track) => track.trackId === trackId)
}

function size(value: number): string {
  return value < 1024 ? `${value} Б` : `${(value / 1024).toFixed(1)} КиБ`
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
      </div>
    </div>

    <div class="panel">
      <h2>Файлы на трекере</h2>
      <p v-if="!remoteTracks.length" class="empty">Подключите трекер и обновите список.</p>
      <div v-else class="track-list">
        <article v-for="track in remoteTracks" :key="track.trackId" class="track-row">
          <div>
            <strong>track_{{ String(track.trackId).padStart(6, '0') }}.c6t</strong>
            <span>{{ size(local(track.trackId)?.bytesReceived ?? 0) }} / {{ size(track.fileSize) }}<template v-if="track.current"> · текущий</template></span>
          </div>
          <progress v-if="busyTrackId === track.trackId" :value="progress ?? 0" :max="track.fileSize"></progress>
          <div class="actions">
            <button :disabled="!connected || busyTrackId != null" @click="$emit('sync', track)">
              {{ local(track.trackId)?.bytesReceived === track.fileSize ? 'Проверить обновления' : 'Синхронизировать' }}
            </button>
            <button :disabled="local(track.trackId)?.bytesReceived !== local(track.trackId)?.fileSize" @click="$emit('view', track.trackId)">На карту</button>
            <button :disabled="local(track.trackId)?.bytesReceived !== local(track.trackId)?.fileSize" @click="$emit('export', track.trackId, 'gpx')">GPX</button>
            <button :disabled="local(track.trackId)?.bytesReceived !== local(track.trackId)?.fileSize" @click="$emit('export', track.trackId, 'geojson')">GeoJSON</button>
          </div>
        </article>
      </div>
    </div>
  </section>
</template>
