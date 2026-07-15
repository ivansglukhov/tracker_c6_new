<script setup lang="ts">
import { computed } from 'vue'
import type { BatterySample } from '../types'

const props = defineProps<{ samples: BatterySample[] }>()

const ordered = computed(() => [...props.samples]
  .filter((sample) => Number.isFinite(sample.batteryPercent))
  .sort((a, b) => (a.gpsEpoch || (a.capturedAt ?? 0) / 1000) - (b.gpsEpoch || (b.capturedAt ?? 0) / 1000)))

const latest = computed(() => ordered.value.at(-1))
const percentRange = computed(() => {
  const values = ordered.value.map((sample) => sample.batteryPercent)
  return values.length ? { min: Math.min(...values), max: Math.max(...values) } : undefined
})
const voltageRange = computed(() => {
  const values = ordered.value.map((sample) => sample.batteryMillivolts).filter(Boolean)
  return values.length ? { min: Math.min(...values), max: Math.max(...values) } : undefined
})
const polyline = computed(() => {
  const values = ordered.value
  if (!values.length) return ''
  return values.map((sample, index) => {
    const x = values.length === 1 ? 500 : 45 + index * 910 / (values.length - 1)
    const y = 215 - sample.batteryPercent * 1.8
    return `${x.toFixed(1)},${y.toFixed(1)}`
  }).join(' ')
})

function voltage(value?: number): string {
  return value ? `${(value / 1000).toFixed(2)} В` : '—'
}
</script>

<template>
  <section class="panel battery-chart">
    <div class="battery-chart-heading">
      <div>
        <h2>Батарея</h2>
        <span class="hint">История измерений трекера и текущего подключения</span>
      </div>
      <strong>{{ latest ? `${latest.batteryPercent}%` : '—' }}</strong>
    </div>
    <svg v-if="ordered.length" viewBox="0 0 1000 240" role="img" aria-label="График заряда батареи">
      <line v-for="value in [0, 25, 50, 75, 100]" :key="value" x1="45" x2="955" :y1="215 - value * 1.8" :y2="215 - value * 1.8" />
      <text v-for="value in [0, 25, 50, 75, 100]" :key="`label-${value}`" x="5" :y="219 - value * 1.8">{{ value }}%</text>
      <polyline :points="polyline" />
    </svg>
    <p v-else class="empty">История появится после подключения или загрузки трека нового формата.</p>
    <div class="battery-chart-stats">
      <span>Сейчас <strong>{{ latest?.batteryPercent ?? '—' }}% · {{ voltage(latest?.batteryMillivolts) }}</strong></span>
      <span>Диапазон <strong>{{ percentRange ? `${percentRange.min}–${percentRange.max}%` : '—' }}</strong></span>
      <span>Напряжение <strong>{{ voltage(voltageRange?.min) }}–{{ voltage(voltageRange?.max) }}</strong></span>
      <span>Замеров <strong>{{ ordered.length }}</strong></span>
    </div>
  </section>
</template>
