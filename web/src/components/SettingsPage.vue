<script setup lang="ts">
import { reactive, ref, watch } from 'vue'
import BatteryChart from './BatteryChart.vue'
import type { BatterySample, DeviceSettings, TrackFilterSettings } from '../types'

interface SettingsProfile {
  device: DeviceSettings
  filters: TrackFilterSettings
  autoReconnect: boolean
}

const CUSTOM_PROFILES_KEY = 'c6-settings-profiles'
const BUILTIN_PROFILES: Record<string, SettingsProfile> = {
  'Пешком': {
    device: { awakeTimeSec: 180, sleepTimeSec: 120, screenOnTimerWake: false, bleOnTimerWake: true, followSleepScheduleWhileBle: true },
    filters: { minSatellites: 0, maxHdop: 0, maxJumpM: 0 },
    autoReconnect: true,
  },
  'Авто': {
    device: { awakeTimeSec: 180, sleepTimeSec: 10, screenOnTimerWake: false, bleOnTimerWake: true, followSleepScheduleWhileBle: true },
    filters: { minSatellites: 4, maxHdop: 5, maxJumpM: 1000 },
    autoReconnect: true,
  },
  'Эконом': {
    device: { awakeTimeSec: 60, sleepTimeSec: 3600, screenOnTimerWake: false, bleOnTimerWake: true, followSleepScheduleWhileBle: false },
    filters: { minSatellites: 0, maxHdop: 0, maxJumpM: 0 },
    autoReconnect: true,
  },
}

const props = defineProps<{
  settings: DeviceSettings
  filterSettings: TrackFilterSettings
  autoReconnect: boolean
  connected: boolean
  pending: boolean
  confirmed: boolean
  batterySamples: BatterySample[]
}>()
const emit = defineEmits<{
  save: [settings: DeviceSettings, filters: TrackFilterSettings, autoReconnect: boolean]
}>()

const device = reactive<DeviceSettings>({ ...props.settings })
const filters = reactive<TrackFilterSettings>({ ...props.filterSettings })
const reconnect = ref(props.autoReconnect)
const selectedProfile = ref('builtin:Пешком')
const customProfiles = ref<Record<string, SettingsProfile>>(loadCustomProfiles())

watch(() => props.settings, (value) => Object.assign(device, value), { deep: true })
watch(() => props.filterSettings, (value) => Object.assign(filters, value), { deep: true })
watch(() => props.autoReconnect, (value) => { reconnect.value = value })

function loadCustomProfiles(): Record<string, SettingsProfile> {
  try {
    const value = JSON.parse(localStorage.getItem(CUSTOM_PROFILES_KEY) || '{}') as Record<string, SettingsProfile>
    return value && typeof value === 'object' ? value : {}
  } catch {
    localStorage.removeItem(CUSTOM_PROFILES_KEY)
    return {}
  }
}

function currentProfile(): SettingsProfile {
  return {
    device: { ...device },
    filters: { ...filters },
    autoReconnect: reconnect.value,
  }
}

function submit(): void {
  const profile = currentProfile()
  emit('save', profile.device, profile.filters, profile.autoReconnect)
}

function applyProfile(): void {
  const [kind, name] = selectedProfile.value.split(':', 2)
  const profile = kind === 'builtin' ? BUILTIN_PROFILES[name] : customProfiles.value[name]
  if (!profile) return
  Object.assign(device, profile.device)
  Object.assign(filters, profile.filters)
  reconnect.value = profile.autoReconnect
  submit()
}

function saveProfile(): void {
  const name = (prompt('Название профиля:', '') || '').trim()
  if (!name) return
  customProfiles.value = { ...customProfiles.value, [name]: currentProfile() }
  localStorage.setItem(CUSTOM_PROFILES_KEY, JSON.stringify(customProfiles.value))
  selectedProfile.value = `custom:${name}`
}

function deleteProfile(): void {
  if (!selectedProfile.value.startsWith('custom:')) return
  const name = selectedProfile.value.slice(7)
  const profiles = { ...customProfiles.value }
  delete profiles[name]
  customProfiles.value = profiles
  localStorage.setItem(CUSTOM_PROFILES_KEY, JSON.stringify(profiles))
  selectedProfile.value = 'builtin:Пешком'
}
</script>

<template>
  <section class="settings-page">
    <BatteryChart :samples="batterySamples" />
    <form class="panel settings" @submit.prevent="submit">
      <h2>Настройки</h2>

      <section class="settings-summary">
        <h3>{{ confirmed && !pending ? 'Настройки, подтверждённые трекером' : 'Действующие настройки на телефоне' }}</h3>
        <ul>
          <li>Бодрствование: {{ device.awakeTimeSec }} с</li>
          <li>Deep Sleep: {{ device.sleepTimeSec }} с</li>
          <li>Экран при таймерном пробуждении: {{ device.screenOnTimerWake ? 'да' : 'нет' }}</li>
          <li>Bluetooth при таймерном пробуждении: {{ device.bleOnTimerWake ? 'да' : 'нет' }}</li>
          <li>Засыпать при подключённом Bluetooth: {{ device.followSleepScheduleWhileBle ? 'да' : 'нет' }}</li>
          <li>Автоподключение: {{ reconnect ? 'да' : 'нет' }}</li>
        </ul>
      </section>

    <section class="settings-section">
      <h3>Профили режима</h3>
      <div class="profile-row">
        <select v-model="selectedProfile" aria-label="Профиль режима">
          <optgroup label="Готовые">
            <option v-for="(_, name) in BUILTIN_PROFILES" :key="name" :value="`builtin:${name}`">{{ name }}</option>
          </optgroup>
          <optgroup v-if="Object.keys(customProfiles).length" label="Свои">
            <option v-for="(_, name) in customProfiles" :key="name" :value="`custom:${name}`">{{ name }}</option>
          </optgroup>
        </select>
        <button type="button" @click="applyProfile">Применить</button>
        <button type="button" @click="saveProfile">Сохранить свой</button>
        <button type="button" :disabled="!selectedProfile.startsWith('custom:')" @click="deleteProfile">Удалить свой</button>
      </div>
    </section>

    <section class="settings-section settings-grid">
      <h3>GPS и сон устройства</h3>
      <label>Окно бодрствования / ожидания GPS, с<input v-model.number="device.awakeTimeSec" type="number" min="1" max="3600"></label>
      <label>Время общего Deep Sleep, с<input v-model.number="device.sleepTimeSec" type="number" min="5" max="86400"></label>
      <label class="check"><input v-model="device.screenOnTimerWake" type="checkbox">Включать экран при таймерном пробуждении</label>
      <label class="check"><input v-model="device.bleOnTimerWake" type="checkbox">Включать Bluetooth при таймерном пробуждении</label>
      <label class="check"><input v-model="device.followSleepScheduleWhileBle" type="checkbox">Засыпать при подключённом Bluetooth</label>
    </section>

    <section class="settings-section settings-grid">
      <h3>Веб-интерфейс</h3>
      <label class="check"><input v-model="reconnect" type="checkbox">Автоматически переподключаться к выбранному трекеру</label>
    </section>

    <section class="settings-section settings-grid">
      <h3>Фильтрация на телефоне</h3>
      <label>Минимум спутников<input v-model.number="filters.minSatellites" type="number" min="0" max="64"></label>
      <label>Максимальный HDOP<input v-model.number="filters.maxHdop" type="number" min="0" max="100" step="0.1"></label>
      <label>Максимальный скачок, м<input v-model.number="filters.maxJumpM" type="number" min="0" max="100000"></label>
      <p class="hint settings-wide">Нулевое значение отключает порог. Трекер продолжает записывать все координаты; эти параметры влияют только на отображение карты.</p>
    </section>

    <p class="hint">GPS работает с частотой 1 Гц всё время бодрствования. Отдельной настройки частоты и минимального числа точек перед сном нет.</p>
    <p v-if="pending" class="pending-settings">Настройки сохранены в телефоне и будут отправлены трекеру после подключения.</p>
      <button class="primary" type="submit">{{ connected ? 'Сохранить на трекер' : 'Сохранить и отправить при пробуждении' }}</button>
    </form>
  </section>
</template>
