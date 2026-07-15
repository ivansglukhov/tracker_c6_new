<script setup lang="ts">
import { reactive, watch } from 'vue'
import type { TrackFilterSettings } from '../types'

const props = defineProps<{ settings: TrackFilterSettings; rawCount: number; filteredCount: number }>()
const emit = defineEmits<{ save: [settings: TrackFilterSettings] }>()
const form = reactive<TrackFilterSettings>({ ...props.settings })

watch(() => props.settings, (value) => Object.assign(form, value), { deep: true })
</script>

<template>
  <form class="panel settings" @submit.prevent="emit('save', { ...form })">
    <h2>Фильтрация трека</h2>
    <p class="hint">Фильтр применяется только к отображению и экспорту в браузере. Исходный файл и блоки IndexedDB не изменяются. Значение 0 отключает соответствующий порог.</p>
    <label>Минимум спутников<input v-model.number="form.minSatellites" type="number" min="0" max="64"></label>
    <label>Максимальный HDOP<input v-model.number="form.maxHdop" type="number" min="0" max="100" step="0.1"></label>
    <label>Максимальный скачок, м<input v-model.number="form.maxJumpM" type="number" min="0" max="100000"></label>
    <p>Точек: {{ rawCount }} исходных · {{ filteredCount }} после фильтра</p>
    <button class="primary" type="submit">Применить</button>
  </form>
</template>
