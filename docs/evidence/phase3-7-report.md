# Phase 3.7 correction report

Пакет: `3.7 — Event, tick, render и switching integration`

Статус: `in_progress` — corrective implementation проверена локально; пакет
будет повторно закрыт только после green normal-push CI точного implementation
commit.

Ветка: `phase-3-work`

## Реализованный scope

- В public SDK опубликован bounded event ABI для Input, SD/media change,
  timer, runtime close и app-private wakeup token без раскрытия `screen_t`.
- Input snapshot переводится в ordered app events через существующий Input
  provider; отдельный hardware/button path не создан.
- Tick и render используют monotonic Time provider и manifest budgets.
- Render получает opaque SDK surface с bounded staging; LCD ownership остаётся у
  injected Display provider.
- Menu open, BACK, autostart, debug forced switch, safe-mode fallback и failure
  unwind проходят через один fixed-capacity switch algorithm.
- Legacy apps сохраняют существующее поведение через adapter того же switch
  boundary и owner cleanup.
- Deferred work защищён generation token; stale generation не может изменить
  state, рисовать или завершить cleanup нового app.
- Host suite покрывает rapid switch, BACK during start, deadline/timeout,
  render failure, autostart fallback и mixed legacy/v2 paths.
- Failure любого running `event`/`tick`/`render` сохраняет исходную ошибку,
  доставляет ровно один `RUNTIME_CLOSE(CALLBACK_FAILED)` и затем выполняет
  общий teardown; ошибка close event не заменяет исходную причину.
- Invalidation, запрошенная внутри `render`, остаётся pending и планирует
  немедленный следующий poll, не ожидая manifest tick interval.
- Новый executable production harness запускает реальные `hk_main.c` и
  `app_runtime_integration.c` с deterministic provider fakes. Он также выявил
  и закрепил исправление production binding: `hk_app_switch_ops_t.user`
  указывает на единственный integration state.

Production app на lifecycle v2 в этом пакете не мигрирован. Feature App SDK
core/host fake из 3.8 и app-scoped services из последующих пакетов не начаты.

## Изменённые contracts и основные файлы

- Public SDK event/surface contract:
  `sdk/include/hackylens/app/runtime.h`.
- Private fixed-capacity runtime:
  `firmware/src/app_runtime/runtime.c`, `surface.c`, `switch.c` и их private
  headers.
- Единственная production integration boundary:
  `firmware/src/runtime/app_runtime_integration.c` и
  `app_runtime_integration.h`.
- Existing firmware paths:
  `firmware/src/runtime/hk_main.c`, `firmware_startup.c`,
  `hk_screen_runtime.c`, `firmware/src/core/hk_menu.c`,
  `firmware/src/controllers/debug_controller.c` и
  `sd_event_controller.c`.
- Owner cleanup/composition:
  `firmware/src/runtime/capability_owner_runtime.c` и generated capability
  inventory binding/generator.
- Normative documentation:
  `docs/spec/APP_RUNTIME.md`, `docs/spec/APP_SDK.md`,
  `docs/APP_LIFECYCLE.md`, ADR-0007 и `docs/CURRENT_STATE.md`.
- Regression/architecture enforcement:
  `tests/app_runtime_mixed_harness.c`, `tests/app_runtime_v2_harness.c`, новый
  `tests/app_runtime_production_harness.c`, `tests/test_app_runtime_v2.py`,
  capability, docs and Phase 3 architecture tests plus their guards.

## Проверки и сборки

- Полный host suite: `272/272` tests passed.
- Дополнительный shared-provider suite: `16/16` tests passed.
- Mixed legacy/v2 runtime harness, lifecycle/latency harness и production
  `hk_main.c` integration harness: passed.
- Documentation governance, Phase 3 architecture boundary, manifest-driven
  composition, capability inventory/composition и Board Port Contract guards:
  passed.
- `hackylens-full` SEN0305 firmware profile: passed; raw image
  `1,562,168 B`.
- `hackylens-feature-modified` с disabled MicroPython: passed; raw image
  `1,367,928 B`.
- Cube conformance compile-check: passed; это не является runtime qualification
  второй платы.

## Resource и latency evidence

Ресурсы сравнивались с exact immutable Phase 2 closure baseline из
`docs/evidence/phase3-baseline.json`.

| Profile | Erase-rounded flash delta | Static RAM delta |
| --- | ---: | ---: |
| full | `+20,480 B` | `+2,576 B` |
| micropython-disabled | `+16,384 B` | `+2,512 B` |

Phase 3 zero-resource gate: passed. Новых direct heap allocation sites,
background tasks, general queues, runtime cores и full-framebuffer
allocations/expressions: `0` по каждой категории.

Representative local host p99 для package-owned runtime paths:

| Path | Observed p99 | Limit |
| --- | ---: | ---: |
| event dispatch | `4 ns` | `100 us` |
| launch | `48 ns` | `100 us` |
| stop | `64 ns` | `100 us` |
| legacy dispatch | `3 ns` | `100 us` |

Это host measurements, а не hardware latency qualification. CI повторно
проверит bounded thresholds для corrective implementation commit.

## Hardware impact

Targeted physical test не требовался. Пакет подключил mixed runtime boundary,
но не мигрировал ни один production app на v2; exit gate 3.7 прямо откладывает
physical behavior check до первой migrated app. Повтор уже принятых Phase 2
UART/I2C/Lights/Files/MicroPython и иных сценариев не имел impact-based
основания.

## Предыдущее closure evidence

Эти commits и runs доказывают первоначальную реализацию, но не заменяют CI для
текущих corrective changes:

- Implementation commit:
  `5fa8e11edeb8a3972092e4e37b69e016ff620ece`.
- Normal-push Release firmware CI для implementation commit:
  [run 33332044807](https://github.com/aztechell/hackylens/actions/runs/33332044807),
  `success`.
- Closure commit:
  `0ecf0fd9478a66c50af1b30706585bbfa7ec1ce3`.
- Normal-push Release firmware CI для closure commit:
  [run 33332339004](https://github.com/aztechell/hackylens/actions/runs/33332339004),
  `success`.

Все восемь scope items остаются реализованными, но пакет 3.7 находится в
`in_progress` до green exact-commit normal-push CI текущей коррекции. Пакет 3.8
в рамках этой работы не начат.
