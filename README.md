# C6 Tracker v2

Чистая реализация трекера на Waveshare ESP32-C6 с нуля.

- `firmware/` — прошивка ESP32-C6.
- `web/` — единый интерфейс на Vue 3: PWA для браузера и Android-обёртка Capacitor.
- `obsidian-vault/` — русскоязычная документация, спецификации и журнал ошибок.

Старый каталог `esp32_c6_tracker` используется только как справочник по аппаратуре.
Код протокола, интерфейса и артефакты сборки в v2 не переносились.

## Проверка

- Прошивка: Arduino CLI с ядром Arduino-ESP32 3.3.10.
- Веб: `npm run build` и `npm test` из каталога `web`.
- Android: `npm run android:sync`, затем `web/android/gradlew.bat assembleDebug`.

Готовый отладочный APK создаётся как
`web/android/app/build/outputs/apk/debug/app-debug.apk`. В Android используется
нативный BLE-транспорт; в обычном Chrome — Web Bluetooth. Формат команд один и
тот же.
