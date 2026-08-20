# HackyLens Phase 2 Masterplan

## Статус и назначение

Статус: `in_progress`.

Исходная ревизия для планирования:
`7183c7ae59008958893c1585ff6cdd96f1fb746b`.

Этот документ — информативный план исполнения Phase 2. Нормативными источниками
остаются [ROADMAP.md](ROADMAP.md),
[ARCHITECTURE_VISION.md](ARCHITECTURE_VISION.md),
[CURRENT_STATE.md](CURRENT_STATE.md),
[BOARD_PORT.md](spec/BOARD_PORT.md),
[GLOSSARY.md](spec/GLOSSARY.md) и
[VERSIONING.md](spec/VERSIONING.md).

Номера `2.1`–`2.14` ниже обозначают последовательные execution-пакеты, а не
заменяют тематические подпункты Phase 2 в roadmap. Каждый пакет рассчитан на
отдельный цикл реализации и review. Следующий пакет не начинается, пока exit
gate предыдущего не подтверждён тестами и tracked evidence, если пакет требует
evidence.

## Итоговая цель

Phase 2 создаёт одну переносимую capability surface для native firmware и
MicroPython adapter. К концу фазы:

- `time`, `input`, `display`, `external-link` и `lights` имеют versioned public
  contracts;
- buttons, display и external-link используются через owner-scoped handles;
- native и MicroPython paths вызывают одни и те же provider implementations;
- selected board/build создаёт immutable capability inventory;
- required capability даёт понятное build exclusion/error, optional capability
  — объявленный fallback;
- apps и MicroPython adapter не зависят от board/BSP, HAL, K210 SDK или drivers;
- fake и K210 adapter проходят один contract suite;
- SEN0305 имеет отдельную physical qualification, а Maix Cube остаётся
  conformance-only.

## Зафиксированные решения

Эти решения считаются исходными ограничениями всех пакетов. Их изменение требует
обновления masterplan и соответствующего ADR до реализации зависимого пакета.

1. Initial public capabilities: `time`, `input`, `display`, `external-link`,
   `lights`.
2. Public storage или frame-pool capability в Phase 2 не вводится. Raw SD I/O
   переносится под существующий storage layer, а `frame_pool` становится private
   portable service.
3. Capability API начинается с `0.1.0 experimental`; целевая версия firmware —
   `0.4.0`.
4. Board Port Contract остаётся `0.1.0`, HMPY — `1.1.0` при wire-major `1`,
   MicroPython API — `1.0.0`, если их наблюдаемые контракты не меняются.
5. Inventory создаётся build generator и неизменяем после boot. Runtime
   registration и capability inference по board ID запрещены.
6. Owners, leases, operations и input events используют fixed-capacity storage;
   новые heap allocations, tasks, queues и cores запрещены.
7. Один absolute monotonic deadline относится ко всей операции и не обновляется
   на chunk, row или retry.
8. Display предоставляет bounded batches, clipping, dirty regions, full-frame
   borrowed surface и фиксированные `BASE`/optional `OVERLAY` planes. Публичный
   экспорт `hk_lcd` запрещён.
9. External-link — одна exclusive connector capability с UART, I2C controller и
   I2C target feature flags. Routing определяется descriptor/build composition.
10. Существующий `hk_app_t` callback ABI не меняется. Private registry wiring
    связывает current native app с owner/handles; полноценный App Runtime остаётся
    Phase 3.
11. Rolling development result никогда автоматически не наследует hardware
    qualification. Hardware smoke обязан ссылаться на immutable closure result и
    exact firmware image, как в
    [Phase 1 closure evidence](evidence/phase1-closure-result.json) и
    [Phase 1 hardware smoke](evidence/phase1-hardware-smoke.json).

## Общие правила выполнения пакетов

Для каждого execution-пакета обязательно:

- сначала подтвердить preconditions и актуальность указанных call sites;
- не исследовать git history как источник требований;
- не сохранять два production hardware paths после exit gate пакета;
- не добавлять временный public facade, который потребуется удалить в Phase 3;
- обновить tests одновременно с изменением контракта или слоя;
- измерить flash/static RAM/latency при runtime-affecting изменении;
- перечислить затронутые public contracts и версии;
- оставить worktree в состоянии, где полный применимый набор проверок зелёный;
- не помечать пакет завершённым только по наличию API signatures.

Статусы задач в этом документе меняются только после выполнения их локального
exit gate:

- `[ ]` — не начато;
- `[~]` — в работе;
- `[x]` — завершено с указанным evidence.

---

## 2.1 — Governance, contracts и Phase 2 baseline

Статус пакета: `completed` (2026-08-14); closure CI `31809871899` прошёл.

### Цель

Зафиксировать public boundary до изменения firmware и создать независимую
resource/performance baseline для Phase 2.

### Scope

- [x] Добавить normative `docs/spec/CAPABILITY_API.md` с metadata:
  `contract-id: hackylens.capability-api`, owner `platform-architecture`, version
  `0.1.0`, stability `experimental`.
- [x] Добавить normative capability sub-specs для time, input, display,
  external-link и lights.
- [x] Зафиксировать canonical numeric IDs, canonical names, version ranges,
  feature flags и extensible-struct rules.
- [x] Добавить два proposed ADR:
  owner-scoped handles/lifetime и generated immutable inventory/discovery.
- [x] Зафиксировать version matrix: Firmware `0.4.0`, Capability API `0.1.0`; без
  необоснованных bumps Board Port, HMPY, MicroPython API или Versioning Policy.
- [x] Создать canonical `docs/evidence/phase2-baseline.json` для текущего main:
  full SEN0305 и MicroPython-disabled build, raw/rounded flash, static RAM,
  source-declared timing bounds и image/ELF/composition/attestation hashes.
  Hardware latency измеряется отдельно в `2.13` и не выдумывается в baseline.
- [x] Не изменять runtime behavior в этом пакете.

### Основные файлы

- `docs/spec/CAPABILITY_API.md`
- `docs/spec/capabilities/*.md`
- `docs/adr/0005-*.md`
- `docs/adr/0006-*.md`
- `docs/evidence/phase2-baseline.json`
- `tools/check_phase2_evidence.py`
- `tests/test_docs_contracts.py`

### Проверки

- `tools/check_docs.py`;
- canonical JSON/hash validation;
- на pinned baseline commit артефакты воспроизводятся вторым запуском; на
  последующих Phase 2 commits `--verify-profile` проверяет attestation integrity
  и resource deltas относительно immutable baseline;
- `git diff` не содержит firmware behavior changes.

### Exit gate

Пакет завершён, когда public decisions reviewable без чтения будущей
implementation, два ADR имеют согласованный статус, а baseline однозначно
отделён от Phase 1 и от будущего hardware-qualified result.

### Rollback boundary

Полный откат только documentation/evidence changes; firmware не затронута.

---

## 2.2 — Common Capability API core

Статус пакета: `completed` (2026-08-14); corrective CI `31818756461` прошёл.

### Depends on

`2.1`.

### Цель

Реализовать общий ABI и lifecycle без hardware-specific capability behavior.

### Scope

- [x] Добавить public типы `hk_result_t`, `hk_deadline_t`, `hk_cancel_t`,
  `hk_owner_t`, `hk_lease_t`, `hk_buffer_view_t`, capability ID/version/request
  и immutable info.
- [x] Добавить opaque typed-handle convention: каждый capability handle содержит
  только validated lease token, без public vtable pointer.
- [x] Реализовать fixed owner/lease tables, generation validation, grant checking,
  acquire/release и owner-wide cleanup.
- [x] Определить точное поведение zero handle, copied stale handle, wrong owner,
  wrong capability type, generation exhaustion и cleanup failure.
- [x] Реализовать affinity validation и provider quarantine/recovery state.
- [x] Добавить private provider interface и deterministic host fake.
- [x] Добавить private current-app owner binding через существующие registry/menu
  entry/exit paths без изменения `hk_app_t` callback signatures.
- [x] Не добавлять реальный display/input/external/lights provider.
- [x] Исправить owner-wide cleanup для mixed-affinity leases и закрыть
  focused regression зелёным corrective CI.

### Основные файлы

- `firmware/include/hackylens/capability/common.h`
- `firmware/include/hackylens/capability/inventory.h`
- `firmware/include/hackylens/capability/owner.h`
- `firmware/src/capabilities/capability_core.c`
- `firmware/src/capabilities/capability_owner.c`
- `firmware/src/capabilities/capability_provider.h`
- `firmware/src/runtime/capability_owner_runtime.c`
- `tests/capability_contract_suite.c`
- `tests/capability_fake_provider.c`

### Проверки

- acquire/release и double-release;
- wrong owner/type, stale copies и inactive owner;
- owner close после частичного acquire;
- fixed table exhaustion без memory corruption;
- affinity mismatch;
- no heap/task/queue symbol check;
- fake проходит общий lifecycle suite.

### Exit gate

Common core полностью тестируется на host, но production inventory ещё может быть
пустым. Ни один app/adapter пока не обязан использовать новый API.

### Rollback boundary

Удаление common core и host tests возвращает прежнее runtime behavior без data
migration.

---

## 2.3 — Build composition и immutable inventory

Статус пакета: `completed` (2026-08-20); implementation CI `32336546168`
прошёл.

### Depends on

`2.2`.

### Цель

Сделать selected board/profile единственным источником реально доступных
capabilities до миграции production consumers.

### Scope

- [x] Повысить private `firmware/app_requirements.toml` до schema 2.
- [x] Сохранить `requires` только для ещё не мигрированных private resources
  (`camera`, `sd-card`, `internal-flash` и аналогичных).
- [x] Добавить `required_capabilities` и `optional_capabilities`; optional entry
  обязан иметь named fallback.
- [x] Добавить `firmware/capability_consumers.toml` для runtime/services/adapters.
- [x] Добавить `platforms/k210/capabilities.toml`, который сопоставляет public IDs
  descriptor resource predicates и provider symbols, не меняя Board Port schema.
- [x] Реализовать `tools/gen_capability_inventory.py`.
- [x] Генерировать const C inventory, owner grants, `capabilities.json` и
  composition schema 2.
- [x] Добавить machine-readable absence reasons: resource absent, driver
  unsupported, route unavailable, provider excluded, version incompatible,
  feature missing.
- [x] Сохранить `--require-app`; required mismatch должен давать deterministic
  build error.
- [x] Добавить recorded diagnostic `--disable-capability`, запрещённый для
  release-qualified build.
- [x] Запретить runtime registration и ручное определение inventory symbol.

### Основные файлы

- `firmware/app_requirements.toml`
- `firmware/capability_consumers.toml`
- `platforms/k210/capabilities.toml`
- `tools/build_firmware.py`
- `tools/gen_capability_inventory.py`
- `tools/check_capabilities.py`
- `tests/test_capability_composition.py`

### Проверки

- deterministic generation;
- duplicate/unknown ID и provider symbol rejection;
- version/feature range matching;
- required exclusion и `--require-app` failure;
- optional fallback selection;
- impossible capability не появляется в C inventory;
- Cube inventory отражает только compile-conformant resources и не получает
  runtime-qualified status.

### Exit gate

Production firmware использует только generated immutable inventory. Добавление
будущего provider невозможно без одновременного resource mapping и generator
validation.

### Rollback boundary

Schema/generator можно откатить целиком до начала consumer migration. После 2.4
он становится обязательной foundation и отдельно не откатывается.

---

## 2.4 — Error/deadline/cancellation model и Time capability

Статус пакета: `completed` (2026-08-20); corrective `ANY_CORE` ordering CI
passed in [`Release firmware` run 32343616128](https://github.com/aztechell/hackylens/actions/runs/32343616128).

### Depends on

`2.3`.

### Цель

Доказать common model на простом shared provider и удалить private time обходы.

### Scope

- [x] Финализировать result code semantics и порядок terminal outcomes.
- [x] Реализовать absolute monotonic deadlines, immediate deadline и rejection
  infinite/over-limit waits.
- [x] Реализовать borrowed cancellation token; callback bounded и allocation-free.
- [x] Добавить `hackylens.cap.time` version `0.1.0` и K210 adapter.
- [x] Добавить `now_us`, `deadline_after_us`, `sleep_until` с cancellation probe не
  реже одного раза в 5 ms.
- [x] Мигрировать все native `time_internal` call sites.
- [x] Мигрировать MicroPython `ticks_ms`/`sleep_ms` без изменения API v1.
- [x] Удалить app/adapter dependence на `internal/time_internal.h`; оставить
  private boot/runtime use только если он не дублирует capability behavior.

### Основные файлы

- `firmware/include/hackylens/capability/time.h`
- `platforms/k210/capabilities/time_adapter.c`
- `firmware/src/internal/time_internal.*`
- time consumers в `firmware/src/apps/`
- `firmware/src/apps/micropython/micropython_bindings.c`
- `tests/test_time_capability.py`

### Проверки

- monotonicity и overflow handling;
- already-expired deadline не вызывает side effect;
- cancel before/during/after completion;
- один deadline сохраняется через все sleep slices;
- native и MicroPython paths достигают одного provider;
- architecture check не находит `time_internal` в apps/adapter.

### Exit gate

Time — первый production capability с shared native/MicroPython semantics. Старый
app-facing time facade больше не существует.

### Rollback boundary

Time consumer migration и provider откатываются вместе; dual time semantics после
пакета не допускаются.

---

## 2.5 — Input/buttons capability

### Depends on

`2.4`.

### Цель

Перенести sampling/debounce/state/events под capability handle, сохранив current
app callback ABI.

### Scope

- [ ] Добавить `hackylens.cap.input` version `0.1.0`.
- [ ] Переместить logical button IDs/masks в public input contract.
- [ ] Оставить raw button driver только источником electrical sample.
- [ ] Sampling period: 10 ms в superloop, без task.
- [ ] Debounce: transition после 20 ms непрерывно стабильного state.
- [ ] Event содержит sequence, timestamp, state, changed, pressed, released и
  dropped count.
- [ ] Добавить explicit fixed ring на 8 events и отдельный cursor на shared lease.
- [ ] При overflow вернуть latest state, dropped count и resync cursor без replay
  устаревших edges.
- [ ] Runtime input dispatcher использует input handle и адаптирует events в
  существующий `hk_input_snapshot_t`.
- [ ] MicroPython `buttons()` использует тот же provider.
- [ ] Перевести app requirements с private `buttons` kind на required input
  capability/features.

### Основные файлы

- `firmware/include/hackylens/capability/input.h`
- `platforms/k210/capabilities/input_adapter.c`
- `firmware/src/drivers/board_buttons.c`
- `firmware/src/drivers/hk_input.h`
- `firmware/src/runtime/hk_main.c`
- `firmware/src/core/hk_app.h`
- `tests/test_input_capability.py`

### Проверки

- bounce, hold, release и simultaneous buttons;
- edge ровно один раз;
- event timestamp и fixed-time debounce не зависят от app tick rate;
- independent cursors;
- overflow/resync;
- native snapshot adapter и MicroPython возвращают один state;
- required input absence исключает app и корректно работает с `--require-app`.

### Exit gate

Ни runtime, ни MicroPython не вызывает старый `hk_input_poll` hardware path.
Existing native app input signatures и Pong control semantics сохранены.

### Rollback boundary

Raw sampling driver сохраняется как implementation detail; capability event
layer и runtime adapter откатываются одним изменением.

---

## 2.6 — Lights capability

### Depends on

`2.5`.

### Цель

Закрыть оставшийся простой Python→driver path и проверить ownership по
непересекающимся resource masks до display/external-link.

### Scope

- [ ] Добавить `hackylens.cap.lights` version `0.1.0`.
- [ ] Feature/channel masks: display backlight, illumination, RGB.
- [ ] Exclusive ownership только пересекающихся channel masks.
- [ ] Public level range `0..1000`; MicroPython v1 adapter сохраняет свои
  observable values.
- [ ] Immediate writes проверяют owner, cancel и deadline до hardware side effect.
- [ ] Release переводит owned channels в implementation safe-off.
- [ ] Settings service после reacquire восстанавливает latest persisted values.
- [ ] Мигрировать settings/camera light services, Sleep и MicroPython.
- [ ] Удалить `hk_lights.h` из MicroPython hardware bridge.

### Основные файлы

- `firmware/include/hackylens/capability/lights.h`
- `platforms/k210/capabilities/lights_adapter.c`
- `firmware/src/services/settings_lights.*`
- `firmware/src/services/camera_light.*`
- `firmware/src/services/micropython_binding_service.c`
- `tests/test_lights_capability.py`

### Проверки

- overlapping/non-overlapping leases;
- range and feature validation;
- no write on cancelled/expired call;
- owner cleanup safe-off;
- persisted state restore;
- native и MicroPython provider identity.

### Exit gate

Все current light consumers используют общий capability implementation;
Python-only lights hardware logic отсутствует.

### Rollback boundary

Settings restore и provider migration откатываются вместе, чтобы не оставить
channels без определённого cleanup owner.

---

## 2.7 — Display contract и fake

### Depends on

`2.6`.

### Цель

Закрыть display semantics host tests до рискованной миграции всех views.

### Scope

- [ ] Добавить `hackylens.cap.display` version `0.1.0`.
- [ ] Зафиксировать runtime info: dimensions, formats, planes, limits, alignment,
  maximum present duration.
- [ ] Определить exclusive `BASE` и optional `OVERLAY` plane leases.
- [ ] Определить batch lifecycle: begin, clip, primitive/text/blit, present,
  retry, abort.
- [ ] Определить borrowed full-frame surface и dirty rect marking.
- [ ] Зафиксировать top-left, half-open rectangles и overflow-safe clipping.
- [ ] Зафиксировать staged/committed state, partial-transfer repair и cleanup.
- [ ] Реализовать deterministic display fake, command log и byte/region counters.
- [ ] Реальный LCD driver и production consumers в этом пакете не менять.

### Основные файлы

- `firmware/include/hackylens/capability/display.h`
- `docs/spec/capabilities/DISPLAY.md`
- `tests/capability_fake_display.c`
- `tests/test_display_contract.py`

### Проверки

- clipping/no-op/coordinate overflow;
- batch, text и dirty-list limits;
- buffer ownership/lifetime;
- base/overlay conflicts;
- present cancellation и retry;
- partial transfer repair state;
- no full-frame promotion when dirty feature is present.

### Exit gate

Fake полностью доказывает contract state machine. Ни одно неоднозначное решение о
buffers, dirty regions или cancel не переносится в K210 implementation package.

### Rollback boundary

Только новый public contract/fake; production display path не затронут.

---

## 2.8 — K210 display implementation и consumer migration

### Depends on

`2.7`.

### Цель

Перевести все native views и MicroPython display на общий provider без потери
dirty rendering и без второго framebuffer.

### Scope

- [ ] Добавить K210 display adapter поверх private ST7789 transport.
- [ ] Разделить raw panel transport и capability state/ownership.
- [ ] Удалить Python-specific `HK_ENABLE_APP_MICROPYTHON`, `lcd_overlay_*` и
  run-ID ownership из driver.
- [ ] Переиспользовать existing shadow framebuffer и bounded overlay
  command/text storage; второй 320x240 framebuffer запрещён.
- [ ] Мигрировать 13 current app views с `hk_lcd.h` на typed display handle.
- [ ] Передавать handle через private app registry/module binding, не через global
  public facade.
- [ ] Заменить app `display_config.h` assumptions на `hk_display_info_t` или
  explicit capability limits (`min_width`, `min_height`, format features).
- [ ] Мигрировать MicroPython clear/text/rect/present на `OVERLAY` plane.
- [ ] Сохранить camera/files full-frame behavior.
- [ ] Сохранить Pong 20 ms fixed-step, catch-up limit и bounded dirty regions.
- [ ] Перевести display requirements на public capability features/limits.

### Основные файлы

- `platforms/k210/capabilities/display_adapter.c`
- `firmware/src/drivers/lcd_st7789.c`
- `firmware/src/drivers/hk_lcd.h`
- view modules в `firmware/src/apps/`
- `firmware/src/apps/pong/pong_view.c`
- `firmware/src/apps/micropython/micropython_bindings.c`
- `tests/test_pong.py`
- `tests/test_lcd_overlay.py`

### Проверки

- fake и K210 adapter проходят один display suite;
- all native view harnesses;
- full-frame camera/files;
- overlay acquire/present/cancel/retry/release;
- Pong incremental frame не выполняет full-screen present;
- transfer bytes не превышают объединение clipped dirty regions;
- no second framebuffer по map/static RAM evidence;
- полный present укладывается в 500 ms, одинаковая workload не медленнее current
  baseline более чем на 10%.

### Exit gate

В apps и MicroPython adapter нет `hk_lcd.h`; native и Python display достигают
одного provider. Старый public-looking LCD driver API больше не является app
surface.

### Rollback boundary

K210 provider, all consumer call sites и driver split откатываются вместе. После
exit gate legacy overlay path не сохраняется как fallback.

---

## 2.9 — External-link contract и fake

### Depends on

`2.8`.

### Цель

Зафиксировать connector-level async semantics до переноса существующих UART/I2C
реализаций.

### Scope

- [ ] Добавить `hackylens.cap.external-link` version `0.1.0`.
- [ ] Features: UART, I2C controller, I2C target.
- [ ] Один exclusive connector lease для shared physical routing.
- [ ] Определить mode configuration и validation без board ID.
- [ ] Определить async operation token: begin, poll, cancel, terminal result.
- [ ] Один in-flight operation на lease; poll выполняет bounded FIFO burst/32
  bytes.
- [ ] UART completion требует empty FIFO и idle shift register.
- [ ] I2C transfer использует один deadline на весь transaction и возвращает
  определённые NACK/timeout/cancel results.
- [ ] TX/RX buffer ownership действует до terminal result.
- [ ] Реализовать deterministic fake с routing/mode/buffer/event log.
- [ ] Production external service и Python implementation пока не менять.

### Основные файлы

- `firmware/include/hackylens/capability/external_link.h`
- `docs/spec/capabilities/EXTERNAL_LINK.md`
- `tests/capability_fake_external_link.c`
- `tests/test_external_link_contract.py`

### Проверки

- unsupported feature/mode;
- exclusive conflict и mode switch with active op;
- UART partial progress/drain;
- I2C NACK/timeout;
- cancellation before and during operation;
- original deadline не меняется между polls;
- no late writes после terminal cancel;
- borrowed buffer lifetime.

### Exit gate

External-link state machine полностью задан и доказан fake tests; K210-specific
register behavior не протёк в public contract.

### Rollback boundary

Только contract/fake changes; current external hardware path не затронут.

---

## 2.10 — External-link implementation и MicroPython convergence

### Depends on

`2.9`.

### Цель

Сделать existing native service и MicroPython единственными клиентами одного
K210 external-link provider и удалить последнюю Python-only hardware
implementation.

### Scope

- [ ] Реализовать K210 adapter из descriptor-defined routing и existing HAL/raw
  drivers.
- [ ] Перенести raw I2C register state machine из
  `micropython_binding_service.c` в provider/private driver layer.
- [ ] Перенести incremental UART write state в provider; один deadline сохраняется
  на весь Python payload, а не на 256-byte chunk.
- [ ] Сделать `external_link_service` native capability consumer для UART/I2C
  target modes.
- [ ] Перед MicroPython raw use normal service добровольно release connector;
  после owner cleanup service reacquires и восстанавливает HMPY mode.
- [ ] MicroPython bridge оставляет только RPC/ticket/cancel transport и public
  capability calls.
- [ ] Переименовать hardware-free service в
  `firmware/src/adapters/micropython/micropython_capability_bridge.*`.
- [ ] Settings объявляет external-link optional с fallback
  `hide-external-link-menu`.
- [ ] MicroPython объявляет required UART+I2C controller features.
- [ ] Не менять External Link Protocol или MicroPython API v1.

### Основные файлы

- `platforms/k210/capabilities/external_link_adapter.c`
- `firmware/src/services/external_link_service.*`
- `firmware/src/services/micropython_binding_service.*`
- `firmware/src/adapters/micropython/micropython_capability_bridge.*`
- `firmware/src/apps/micropython/micropython_bindings.c`
- `tests/test_micropython_bindings.py`

### Проверки

- fake и K210 adapter проходят один external-link suite;
- existing native protocol service behavior;
- UART FIFO drain и no-late-write cancellation;
- I2C controller и target modes;
- external service release/reacquire around MicroPython run;
- idempotent cleanup order;
- full Python v1 binding harness;
- adapter object не импортирует board/HAL/SDK/driver symbols;
- native и Python paths фиксируют одинаковый provider ID.

### Exit gate

`micropython_binding_service.c` как hardware implementation удалён. Оставшийся
bridge не содержит board/HAL/SDK/driver includes или raw peripheral logic.

### Rollback boundary

Native external service, provider и MicroPython bridge мигрируются/откатываются
как одна связанная единица, чтобы routing ownership не раздвоился.

---

## 2.11 — Residual app→driver cleanup и architecture guard v2

### Depends on

`2.10`.

### Цель

Включить полный запрет app/adapter→drivers без ложных исключений.

### Scope

- [ ] Перенести пять direct `hk_sd.h` call sites в permanent storage operations.
- [ ] Переместить `drivers/frame_pool.*` в private portable
  `services/frame_pool.*`; зафиксировать borrow/release semantics.
- [ ] Убедиться, что apps не включают capability implementation/private headers.
- [ ] Добавить explicit layer classification в
  `tools/architecture_layers.toml`.
- [ ] Расширить `check_arch.py` на `firmware/include`, `boards`, `platforms`,
  generated dependency files и object undefined symbols.
- [ ] Запретить apps→board/BSP, HAL/SDK, drivers.
- [ ] Запретить MicroPython adapter→board/HAL/SDK/drivers.
- [ ] Запретить capability implementation→apps, service→feature-private и
  board→apps/product policy.
- [ ] Запретить ручной provider inventory и Python-gated hardware provider.
- [ ] Заменить Phase 1 test, который разрешает app→driver, на Phase 2 negative
  rule.

### Основные файлы

- current SD consumers в `firmware/src/apps/camera`, `apps/files`,
  `apps/qr_camera`
- `firmware/src/drivers/frame_pool.*`
- `firmware/src/services/frame_pool.*`
- `tools/check_arch.py`
- `tools/architecture_layers.toml`
- `tests/test_board_ports.py`
- `tests/test_phase2_architecture.py`

### Проверки

- repository dependency graph содержит ноль forbidden edges;
- adversarial includes: `..`, backslashes, case, macros, forwarding headers,
  symlinks и transitive includes;
- hand-declared HAL extern обнаруживается symbol check;
- misleading local `apps/.../drivers/...` не обходит классификацию;
- provider object hash одинаков в full и MicroPython-disabled builds;
- storage/camera/files regression tests проходят.

### Exit gate

Architecture guard v2 проходит без allowlist для app→driver или adapter→driver.
Все правила доказываются отдельными negative fixtures.

### Rollback boundary

Storage/frame-pool migration должна быть зелёной до включения hard guard. Сам
guard не включается частично и не содержит временных исключений для известных
call sites.

---

## 2.12 — Full automated qualification и rolling evidence

### Depends on

`2.11`.

### Цель

Собрать все contracts, builds, guards и budgets в один machine-checkable
pre-hardware gate.

### Scope

- [ ] Добавить один contract suite, запускаемый против fake и K210 adapters.
- [ ] Собрать SEN0305 full и MicroPython-disabled configurations.
- [ ] Собрать diagnostic per-capability-absent configurations.
- [ ] Проверить required exclusion, `--require-app` и optional fallback.
- [ ] Выполнить Cube compile-conformance без runtime/hardware claims.
- [ ] Измерить raw/rounded flash, static RAM, relevant stack и latency.
- [ ] Проверить отсутствие новых heap/task/queue/core resources.
- [ ] Создать rolling `docs/evidence/phase2-result.json`.
- [ ] Добавить `tools/check_phase2_resources.py` и pre-hardware
  `tools/check_phase2_exit.py`.

### Hard budgets относительно 2.1 baseline

- erase-rounded flash: не более `+32 KiB`;
- static RAM: не более `+4096 B`;
- новые heap allocations: `0`;
- новые tasks/queues/cores: `0`;
- дополнительный full display framebuffer: `0`.

Любое повышение budget требует отдельного review и обновления masterplan до
принятия изменения. Искусственный RAM padding запрещён.

### Latency gates

- pure registry/validation overhead p99 не более 100 us на SEN0305;
- input sample 10 ms, debounce 20 ms, publication не позже следующего sample;
- full display present не более 500 ms;
- одинаковая display workload не медленнее baseline более чем на 10%;
- external poll slice не более 500 us;
- deadline/cancel terminal notification не позже исходного deadline плюс один
  declared poll interval, максимум 5 ms;
- Pong incremental frame не выполняет full-screen present.

### Проверки

- полный host test suite;
- documentation, architecture, composition и evidence guards;
- full/disabled/conformance build matrix;
- deterministic evidence regeneration;
- rolling result не содержит hardware-qualified status.

### Exit gate

`tools/check_phase2_exit.py` проходит в pre-hardware mode, а rolling result
однозначно описывает exact source/build composition без claims о физическом
устройстве.

### Rollback boundary

Этот пакет не меняет hardware behavior; он делает уже реализованные свойства
обязательными в CI.

---

## 2.13 — Physical SEN0305 acceptance

### Depends on

`2.12`.

### Цель

Подтвердить capability behavior на конкретном SEN0305 image и не переносить
hardware status на будущие binaries.

### Preconditions

- [ ] Назначен candidate closure image с SHA-256 и composition hash.
- [ ] Зафиксированы board serial/revision, toolchain и operator.
- [ ] Доступен external-link fixture: UART TX/RX loopback и известный 7-bit I2C
  target; fixture имеет ID и документированную схему.

### Physical smoke

- [ ] Buttons: каждый logical button, press/release/hold, отсутствие повторных
  edges, измеренная debounce/event latency.
- [ ] Display: menu, затронутые native views, camera/files full-frame, Pong dirty
  frames, MicroPython overlay/cancel/retry/cleanup.
- [ ] UART: TX/RX loopback, wire-drain completion, cancel без late bytes.
- [ ] I2C: controller read/write, mode switch и error recovery.
- [ ] Native external/HMPY service восстанавливается после MicroPython cleanup.
- [ ] Lights: backlight, illumination, RGB, cleanup и persisted restore.
- [ ] Regression: camera, SD read/write/delete, files decode/frame pool, settings,
  sleep, HMPY и boot.
- [ ] Записаны timings, raw logs и при необходимости screen/frame artifacts.

### Evidence

- `docs/evidence/phase2-hardware-smoke.json`;
- hardware artifacts под `docs/evidence/phase2-hardware/`;
- точная ссылка на candidate closure result и firmware image hash.

### Exit gate

Все обязательные physical checks прошли на одном exact image. Maix Cube в этом
пакете не квалифицируется. Если нет external fixture, Phase 2 остаётся открытым,
даже когда host external-link tests зелёные.

### Rollback boundary

Hardware smoke ничего не изменяет в source. Любой новый binary после smoke
требует нового closure selection и не наследует этот статус.

---

## 2.14 — Closure, versions и roadmap status

### Depends on

`2.13`.

### Цель

Закрыть Phase 2 только после связывания source, automated evidence и physical
evidence в одну проверяемую цепочку.

### Scope

- [ ] Повторно выполнить полный automated gate на exact closure commit.
- [ ] Создать immutable `docs/evidence/phase2-closure-result.json`.
- [ ] Проверить, что hardware smoke ссылается на exact closure result/image.
- [ ] Установить Firmware version `0.4.0` и проверить canonical version sources.
- [ ] Обновить `docs/CURRENT_STATE.md`, `docs/MODULES.md` и Phase 2 status в
  `docs/ROADMAP.md` без overclaim hardware independence.
- [ ] Добавить Capability API в normative spec index.
- [ ] Перевести ADR-0005/0006 в фактический итоговый status.
- [ ] Запустить final `tools/check_phase2_exit.py` в closure mode.

### Machine-checkable final gate

- [ ] Capability API `0.1.0 experimental`, Firmware `0.4.0`.
- [ ] Required public contracts и ADR metadata валидны.
- [ ] Inventory deterministic и не содержит fabricated capabilities.
- [ ] Fake и K210 adapters проходят один contract suite.
- [ ] Buttons/display/external-link работают через handles.
- [ ] Lights также полностью мигрирован.
- [ ] Native и MicroPython используют одинаковые providers.
- [ ] Ноль forbidden app/adapter dependency edges.
- [ ] Ноль прямых app→`hk_sd`/`frame_pool` dependencies.
- [ ] Pong fixed-step и dirty-region gates проходят.
- [ ] Full/disabled SEN0305 и Cube conformance builds проходят.
- [ ] Resource/latency budgets соблюдены.
- [ ] Closure и hardware evidence имеют согласованные hashes.

### Exit gate

Phase 2 может быть помечен `done` только когда все пункты `2.1`–`2.14` имеют
статус `[x]`, final checker зелёный и никакой development result не выдаётся за
hardware-qualified image.

### Rollback boundary

После closure любое изменение public Capability API, provider behavior,
composition или qualified image открывает новую version/evidence работу; нельзя
редактировать immutable closure result задним числом.

## Отложено в Phase 3+

- public App Runtime context и lifecycle v2;
- public app manifest/project format;
- Feature SDK и app generator;
- Program Manager/dynamic program loading;
- MicroPython API v2/discovery surface;
- public storage/camera/vision/AI capabilities;
- second-board runtime qualification;
- заявление о полной hardware independence.

## Как продолжать работу

Каждый следующий implementation turn должен называться по execution-пакету,
например: `Phase 2.1`, `Phase 2.5` или `Phase 2.8`. Перед изменением файлов нужно
прочитать этот пакет, его dependencies и связанные normative sources. Итог
каждого turn должен сообщать:

1. какие checklist items завершены;
2. какие files/contracts изменены;
3. какие tests/builds/evidence прошли;
4. resource/latency delta, если применимо;
5. можно ли ставить `[x]` всему пакету или что остаётся открытым.
