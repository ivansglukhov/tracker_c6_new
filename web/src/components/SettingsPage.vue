<script setup lang="ts">
import { reactive, watch } from 'vue'
import type { DeviceSettings } from '../types'

const props = defineProps<{ settings: DeviceSettings; connected: boolean }>()
const emit = defineEmits<{ save: [settings: DeviceSettings] }>()
const form = reactive<DeviceSettings>({ ...props.settings })

watch(() => props.settings, (value) => Object.assign(form, value), { deep: true })
</script>

<template>
  <form class="panel settings" @submit.prevent="emit('save', { ...form })">
    <h2>Настройки устройства</h2>
    <label>Время бодрствования, с<input v-model.number="form.awakeTimeSec" type="number" min="1" max="3600"></label>
    <label>Время общего Deep Sleep, с<input v-model.number="form.sleepTimeSec" type="number" min="5" max="86400"></label>
    <label class="check"><input v-model="form.screenOnTimerWake" type="checkbox">Экран при таймерном пробуждении</label>
    <label class="check"><input v-model="form.bleOnTimerWake" type="checkbox">Bluetooth при таймерном пробуждении</label>
    <label class="check"><input v-model="form.followSleepScheduleWhileBle" type="checkbox">Учитывать график сна для GPS при BT</label>
    <p class="hint">GPS выдаёт координаты раз в секунду всё время бодрствования. Частота записи отдельно не настраивается. При включении последнего пункта весь трекер уходит в Deep Sleep даже при подключённом Bluetooth, а веб-интерфейс переподключается после пробуждения.</p>
    <button class="primary" :disabled="!connected" type="submit">Сохранить</button>
  </form>
</template>
