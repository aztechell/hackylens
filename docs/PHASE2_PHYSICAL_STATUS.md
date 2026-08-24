# Phase 2 physical status

Статус на 2026-08-24. Это короткий рабочий ledger, а не требование повторять
весь smoke после каждого коммита. Каждый выполненный тест сохраняет identity
образа и переносится вперёд только после impact review.

## Правило повторного тестирования

- Всегда: полный automated CI и boot sanity текущего closure build.
- Повторить targeted physical test, если diff затрагивает соответствующий
  capability/provider/HAL/routing, его composition или ресурсные assumptions.
- Повторить более широкий набор только для board, toolchain или неясного
  cross-cutting изменения.
- UI/app-only diff не обнуляет UART, I2C, TIME или lights observations.
- Старый результат не переписывается: в финальном evidence остаётся исходный
  tested commit/image hash и краткое обоснование carry-forward.

## Уже выполнено

| Область | Физический результат | Статус |
| --- | --- | --- |
| Boot/debug | SEN0305 загружается как 0.4.0; `HKPING` возвращает `HKPONG` | done |
| TIME | `ticks_ms`, sleep и bounded cancellation прошли после corrective | done |
| UART loopback | Повторные byte-distinct передачи вернули 256/256; cancellation и чистая post-cancel передача прошли | done |
| Native/HMPY restore | HMPY работал до и после MicroPython raw use/cancellation без reboot | done |
| I2C controller | Nano target `0x42`: write/read, prefix lengths 1/2/6/7/8, bounded NACK recovery | done |
| I2C stress | 100/100 циклов с периодическим NACK и восстановлением, включая работу при запущенной camera | done |
| Camera/display throughput | Camera стабильно 25.4–25.5 FPS; present около 31.27 ms; Files full-frame визуально проверен | done |
| Menu | Навигация работает; хвост `T` после `OBJECT DETECT` устранён | done |
| Pong | Мяч выходит из serve state и игра движется | done |
| Sleep regression | Wake больше не приводит к немедленному повторному sleep | done |
| Buttons | LEFT/OK/RIGHT/BACK прошли press, hold, release и no-repeat через встроенный `BUTTON TEST` | done |
| Files/SD | Папки листаются и открываются; файлы открываются и записываются | done |
| Lights | Backlight, illumination и RGB визуально работают на текущем Phase 2 build | done |

UART/I2C/TIME код не изменялся в `89f6141..06c8433`; эти observations
переносятся. Коммит `06c8433` затронул только Pong TIME request, menu title
cleanup, tests и rolling evidence; его targeted Pong/menu и boot checks прошли,
а CI run `32697828729` зелёный.

## Частично выполнено

| Область | Что подтверждено | Что ещё нужно |
| --- | --- | --- |
| Display | Menu, camera full-frame, Files и Pong проверены | Физический MicroPython overlay cleanup/retry |
| UART | Payload и cancellation проверены | Отдельное инструментальное доказательство момента wire-drain не записано |
| I2C/mode switching | Controller и NACK recovery проверены | Явный UART→I2C→UART round trip без reboot не записан |
| Lights | Backlight, illumination и RGB проверены на Phase 2 build | Safe-off cleanup и persisted restore отдельно не проверены |
| Regression | Boot, camera, SD/Files read/write, sleep и HMPY проверены | SD delete, frame-pool reuse и settings persistence |
| Evidence | Локальные UART/HMPY/I2C scripts и результаты сохранены в `build/` | Перед closure перенести только нужные короткие raw logs в tracked evidence |

## Не запускалось физически

- frame-pool reuse сценарий;
- SD delete сценарий на Phase 2 build;
- MicroPython overlay success/cancel/retry/cleanup с визуальным подтверждением;
- lights safe-off cleanup и persisted restore;
- численные button debounce/event latency samples;
- matched-workload display regression dataset для всех требуемых views.

Эти пункты не требуют повторять уже зелёные UART/I2C/TIME проверки.
