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
| Camera/display throughput | Camera стабильно 25.4–25.5 FPS; present около 31.27 ms | done |
| Menu | Навигация работает; хвост `T` после `OBJECT DETECT` устранён | done |
| Pong | Мяч выходит из serve state и игра движется | done |
| Sleep regression | Wake больше не приводит к немедленному повторному sleep | done |

UART/I2C/TIME код не изменялся в `89f6141..06c8433`; эти observations
переносятся. Коммит `06c8433` затронул только Pong TIME request, menu title
cleanup, tests и rolling evidence; его targeted Pong/menu и boot checks прошли,
а CI run `32697828729` зелёный.

## Частично выполнено

| Область | Что подтверждено | Что ещё нужно |
| --- | --- | --- |
| Buttons | Left/Right/OK/Back использовались для реальной навигации | Отдельный press/release/hold/no-repeat check; latency нужна только если хотим численную qualification |
| Display | Menu, camera full-frame и Pong проверены | Files full-frame и физический MicroPython overlay cleanup/retry |
| UART | Payload и cancellation проверены | Отдельное инструментальное доказательство момента wire-drain не записано |
| I2C/mode switching | Controller и NACK recovery проверены | Явный UART→I2C→UART round trip без reboot не записан |
| Lights | Phase 1 подтверждал illumination/RGB на этой SEN0305 | Phase 2 safe-off cleanup и persisted restore отдельно не проверены |
| Regression | Boot, camera, sleep и HMPY проверены | Полный SD read/write/delete, Files decode/frame-pool и settings persistence на Phase 2 build |
| Evidence | Локальные UART/HMPY/I2C scripts и результаты сохранены в `build/` | Перед closure перенести только нужные короткие raw logs в tracked evidence |

## Не запускалось физически

- полный Files decode/frame-pool сценарий;
- полный SD read/write/delete сценарий на Phase 2 build;
- MicroPython overlay success/cancel/retry/cleanup с визуальным подтверждением;
- lights safe-off cleanup и persisted restore;
- численные button debounce/event latency samples;
- matched-workload display regression dataset для всех требуемых views.

Эти пункты не требуют повторять уже зелёные UART/I2C/TIME проверки.
