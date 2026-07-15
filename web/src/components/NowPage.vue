<script setup lang="ts">
import type { DeviceStatus, LivePoint } from '../types'

defineProps<{ status?: DeviceStatus; point?: LivePoint }>()

function distance(value?: number): string {
  return value == null ? '—' : `${(value / 1000).toFixed(2)} км`
}
</script>

<template>
  <section v-if="status" class="metric-grid">
    <article><span>Текущий трек</span><strong>{{ status.trackId }}</strong></article>
    <article><span>Сырая дистанция</span><strong>{{ distance(status.distanceM) }}</strong></article>
    <article><span>Точек</span><strong>{{ status.pointCount }}</strong></article>
    <article><span>Батарея</span><strong>{{ status.batteryPercent }}%<small v-if="status.batteryMillivolts"> · {{ (status.batteryMillivolts / 1000).toFixed(2) }} В</small></strong></article>
    <article><span>GPS / спутники</span><strong>{{ status.flags & 1 ? 'координата' : 'ожидание' }} / {{ status.satellites }}</strong></article>
    <article><span>SD</span><strong>{{ status.flags & 4 ? 'ошибка' : status.flags & 2 ? 'готова' : 'нет' }}</strong></article>
    <article><span>Высота</span><strong>{{ point?.altitudeM ?? status.altitudeM }} м</strong></article>
    <article><span>Окно BT</span><strong>{{ status.interactiveRemainingSec ? `${status.interactiveRemainingSec} с` : 'завершено' }}</strong></article>
  </section>
  <p v-else class="empty">Подключите трекер, чтобы получить телеметрию.</p>
</template>

