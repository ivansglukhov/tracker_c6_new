<script setup lang="ts">
import type { DeviceStatus, LivePoint } from '../types'

defineProps<{
  status?: DeviceStatus
  point?: LivePoint
  connected: boolean
  nmeaEnabled: boolean
  nmeaGga: string
  nmeaUpdatedAt?: number
}>()

const emit = defineEmits<{ nmea: [enabled: boolean] }>()

function distance(value?: number): string {
  return value == null ? '—' : `${(value / 1000).toFixed(2)} км`
}
</script>

<template>
  <section v-if="status" class="metric-grid">
    <article><span>Текущий трек</span><strong>{{ status.trackId }}</strong></article>
    <article><span>Сырая дистанция</span><strong>{{ distance(status.distanceM) }}</strong></article>
    <article><span>Точек</span><strong>{{ status.pointCount }}</strong></article>
    <article><span>Точки текущего цикла</span><strong>{{ status.cyclePointCount }} / {{ status.pointsBeforeSleep }}</strong></article>
    <article><span>Батарея</span><strong>{{ status.batteryPercent }}%<small v-if="status.batteryMillivolts"> · {{ (status.batteryMillivolts / 1000).toFixed(2) }} В</small></strong></article>
    <article><span>GPS / спутники</span><strong>{{ status.flags & 1 ? 'координата' : 'ожидание' }} / {{ status.satellites }}</strong></article>
    <article><span>SD</span><strong>{{ status.flags & 4 ? 'ошибка' : status.flags & 2 ? 'готова' : 'нет' }}</strong></article>
    <article><span>USB</span><strong>{{ status.flags & 16 ? 'подключён · сон запрещён' : 'нет' }}</strong></article>
    <article><span>Высота</span><strong>{{ point?.altitudeM ?? status.altitudeM }} м</strong></article>
    <article><span>Окно BT</span><strong>{{ status.interactiveRemainingSec ? `${status.interactiveRemainingSec} с` : 'завершено' }}</strong></article>
  </section>
  <section v-if="status" class="panel nmea-panel">
    <label class="check">
      <input
        type="checkbox"
        :checked="nmeaEnabled"
        :disabled="!connected"
        @change="emit('nmea', ($event.target as HTMLInputElement).checked)"
      >
      Диагностика GPS (NMEA GGA)
    </label>
    <p class="hint">Не влияет на запись трека и не препятствует переходу в сон.</p>
    <p v-if="nmeaGga" class="nmea-state">GPS передаёт данные · {{ nmeaUpdatedAt ? new Date(nmeaUpdatedAt).toLocaleTimeString('ru-RU') : '' }}</p>
    <code v-if="nmeaGga" class="nmea-line">{{ nmeaGga }}</code>
    <p v-else-if="nmeaEnabled" class="hint">Ожидание строки GGA…</p>
  </section>
  <p v-else class="empty">Подключите трекер, чтобы получить телеметрию.</p>
</template>

