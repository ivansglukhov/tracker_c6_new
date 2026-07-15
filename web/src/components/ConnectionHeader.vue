<script setup lang="ts">
defineProps<{ connected: boolean; busy: boolean; statusText: string }>()
defineEmits<{ connect: []; disconnect: [] }>()
</script>

<template>
  <header class="app-header">
    <div>
      <h1>C6 Tracker</h1>
      <span :class="['connection', { online: connected }]">{{ statusText }}</span>
    </div>
    <button v-if="!connected" class="primary" :disabled="busy" @click="$emit('connect')">Подключить</button>
    <button v-else @click="$emit('disconnect')">Отключить</button>
  </header>
</template>

