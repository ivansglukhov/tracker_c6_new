import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import { VitePWA } from 'vite-plugin-pwa'

export default defineConfig({
  plugins: [
    vue(),
    VitePWA({
      registerType: 'autoUpdate',
      includeAssets: ['tracker-icon.svg'],
      manifest: {
        name: 'C6 Tracker',
        short_name: 'C6 Tracker',
        description: 'Офлайн-карта, треки и управление GPS-трекером ESP32-C6',
        theme_color: '#0a0d0b',
        background_color: '#0a0d0b',
        display: 'standalone',
        orientation: 'any',
        icons: [{ src: 'tracker-icon.svg', sizes: 'any', type: 'image/svg+xml', purpose: 'any maskable' }],
      },
      workbox: {
        navigateFallback: 'index.html',
        runtimeCaching: [{
          urlPattern: /^https:\/\/(?:tile\.openstreetmap\.org|[abc]\.tile-cyclosm\.openstreetmap\.fr|[abcd]\.basemaps\.cartocdn\.com)\//,
          handler: 'CacheFirst',
          options: {
            cacheName: 'map-viewed-tiles',
            expiration: { maxEntries: 1200, maxAgeSeconds: 7 * 24 * 60 * 60 },
            cacheableResponse: { statuses: [0, 200] },
          },
        }],
      },
    }),
  ],
  base: './',
})

