import type { CapacitorConfig } from '@capacitor/cli'

const config: CapacitorConfig = {
  appId: 'ru.ivansglukhov.c6tracker',
  appName: 'C6 Tracker',
  webDir: 'dist',
  plugins: {
    BluetoothLe: {
      displayStrings: {
        scanning: 'Поиск трекера…',
        cancel: 'Отмена',
        availableDevices: 'Доступные устройства',
        noDeviceFound: 'Трекер не найден',
      },
    },
  },
}

export default config
