# HackyLens Phase 3 Masterplan

## Статус и назначение

Статус: `planned`.

Исходная ревизия для планирования:
`ed2adcebb757ccf4c8bdaf5b7ba3f0b9c596eedb`.

Этот документ — поэтапный план исполнения Phase 3. Нормативными источниками
остаются [ROADMAP.md](ROADMAP.md),
[ARCHITECTURE_VISION.md](ARCHITECTURE_VISION.md),
[CURRENT_STATE.md](CURRENT_STATE.md),
[CAPABILITY_API.md](spec/CAPABILITY_API.md),
[GLOSSARY.md](spec/GLOSSARY.md) и
[VERSIONING.md](spec/VERSIONING.md).

Номера `3.1`–`3.17` ниже — последовательные execution-пакеты. Они детализируют,
но не заменяют тематические разделы `3.1`–`3.6` нормативного roadmap. Один turn
выполняет только один пакет, если пользователь явно не расширил scope.

## Итоговая цель

Phase 3 делает native feature app самостоятельной переносимой единицей. К концу
фазы:

- App Runtime v2 выполняет `probe/prepare/start/event/tick/render/stop/cleanup`;
- public `hk_app_context_t` выдаёт только объявленные capability/service handles;
- каждый native app имеет build-time manifest; TOML не парсится на устройстве;
- manifest генерирует registry, build composition, capability grants, menu,
  autostart, debug/help, resource и test metadata;
- ручная таблица в `apps/app_registry.c` больше не является source of truth;
- public Feature App SDK `0.1.0 experimental` живёт только в `sdk/include`;
- SDK имеет host fake, app-scoped settings/storage/logging и camera/frame
  contracts, необходимые для migration proof;
- `tools/new_app.py` создаёт buildable и testable app без ручного изменения core,
  registry или build script;
- BUTTONS, PONG и один camera app работают на lifecycle v2 через injected
  capabilities;
- legacy apps могут временно жить через явный compatibility adapter, но не
  получают обходов manifest, ownership или architecture guard;
- Phase 2 capability contracts и подтверждённое firmware behavior не ломаются.

## Граница Phase 3

В Phase 3 входят только native App Runtime, native app manifest, Feature App SDK,
generator и migration proof. Не входят:

- Project Format и project manifest из Phase 4;
- on-device Program Manager, package install/update и dynamic loading;
- MicroPython API v2 и Python project workflow;
- Python-to-native conversion workflow;
- IDE beta;
- dynamic ELF plugins, MMU, heap-backed registries или runtime TOML parser;
- обязательная миграция всех двенадцати существующих apps на lifecycle v2;
- vision/AI public APIs, если они не нужны выбранному camera migration proof;
- runtime qualification второй K210-платы.

Native app manifest и будущий project manifest — разные контракты. Phase 3 не
должна заранее проектировать Phase 4 внутри `app.toml`.

## Зафиксированные решения

1. App Runtime, App Manifest и App SDK начинают отдельные линии версий с
   `0.1.0 experimental`.
2. Candidate firmware version Phase 3 — `0.5.0`; окончательно она фиксируется в
   `3.17` после version review. HMPY, Board Port, Capability API и MicroPython API
   не меняют версии без изменения их наблюдаемого контракта.
3. Manifest читается только build tooling. Firmware получает immutable generated
   descriptors и не содержит TOML parser, filesystem discovery или runtime app
   registration.
4. ID, stable autostart ID и menu order задаются явно. Порядок файлов, линковки
   или массива не создаёт persistent identity.
5. Required capability несовместимого build исключает app либо даёт понятную
   build error для explicitly required app. Optional capability имеет именованный
   fallback.
6. Runtime не выделяет app state из heap. App предоставляет private fixed-capacity
   state storage, а generated descriptor публикует его размер и alignment для
   проверки manifest limits.
7. В каждый момент есть не более одного foreground v2 app. Deferred cleanup
   допускается только через bounded runtime-owned token/epoch; старый callback не
   может использовать новый context или state.
8. Runtime dispatch синхронный и bounded. Новые tasks, cores, general queues и
   скрытые background loops запрещены.
9. Screen/menu — adapters runtime. `screen_t` может сохраняться внутри legacy
   adapter, но не становится public app identity v2.
10. Capability handles выдаются owner-scoped и generation-checked. Нормативный
    teardown имеет один порядок: `stop(ctx)` → app `cleanup(ctx)` при ещё валидных
    context/handles → runtime owner-wide capability/service cleanup → provider
    quarantine по Phase 2 при failed provider cleanup → invalidation всех
    owner-scoped handles → invalidation context generation → app state становится
    reusable. App cleanup failure не пропускает runtime owner-wide cleanup.
11. Новая camera capability обязана использовать существующий K210 camera/frame
    implementation. После её migration package не остаётся второй production
    hardware path для того же поведения.
12. Storage SDK не открывает raw SD blocks и не даёт app произвольный platform
    path. Settings, storage и logging используют app-scoped namespaces и bounded
    operations.
13. Existing MicroPython API v1 и HMPY остаются совместимыми. Если Phase 3
    публикует capability, уже существующий Python consumer того же hardware не
    должен сохранять привилегированный параллельный provider path.
14. Full physical regression после каждого package не нужен. Повторяются только
    затронутые runtime/app/provider сценарии; незатронутые Phase 2 observations
    переносятся после impact review и green automated CI.
15. `sdk/include` — public entry surface Feature App SDK. SDK headers могут
    зависеть от и re-export public Phase 2 Capability API types из
    `firmware/include/hackylens/capability/`; это единственное разрешённое
    SDK→Capability API направление. Apps не включают implementation/provider/
    private capability headers. Redundant `hk_app_time_t`/`hk_app_display_t`
    wrappers без конкретной ABI-причины не вводятся.

## Общие правила выполнения

Для каждого execution-пакета:

- сверить текущие call sites и generated build path до изменения source;
- одновременно обновить normative contract, implementation и contract tests,
  если пакет меняет public behavior;
- не добавлять temporary public facade рядом с будущим SDK;
- не оставлять два sources of truth для manifest/registry/composition;
- сохранять fixed-capacity, allocation-free runtime design;
- измерять flash/static RAM и critical dispatch latency для runtime changes;
- прогонять только релевантные host suites во время разработки и полный CI перед
  переводом пакета в `completed`;
- делать physical test только при фактическом hardware/UI/runtime impact;
- не требовать отдельный ceremonial closure commit для каждого пакета. Отдельный
  evidence commit нужен только там, где immutable candidate identity действительно
  является частью gate.

Статусы:

- `[ ]` — не начато;
- `[~]` — в работе;
- `[x]` — exit gate выполнен и соответствующий CI зелёный.

---

## 3.1 — Governance, contracts и Phase 3 baseline

Статус пакета: `completed`.

### Цель

Зафиксировать public boundary Phase 3 до изменения runtime и сохранить измеримый
Phase 2 closure как baseline.

### Scope

- [x] Добавить normative contracts `APP_RUNTIME.md`, `APP_MANIFEST.md` и
  `APP_SDK.md` с metadata `0.1.0 experimental`.
- [x] Добавить ADR для lifecycle/context ownership и ADR для manifest-generated
  composition.
- [x] Нормативно закрепить SDK/Capability API boundary: Feature App SDK является
  app-facing entry surface, использует public capability types и не открывает
  implementation/provider/platform headers.
- [x] Зафиксировать state machine, callback ordering, failure unwind, stop reason,
  cleanup и stale-callback semantics.
- [x] Зафиксировать teardown ordering: idempotent `stop`, app cleanup at most once
  при валидном context, обязательный runtime owner cleanup, provider quarantine
  при его failure, затем handle/context invalidation и state reuse.
- [x] Зафиксировать разницу native app manifest и Phase 4 project format.
- [x] Снять Phase 3 flash/static RAM/dispatch baseline с exact Phase 2 closure и
  утвердить численные budgets до runtime implementation.
- [x] Расширить architecture layer map для `sdk`, `app-runtime`, `manifest` и
  `generated-app-registry` без allowlist для конкретных apps.
- [x] Добавить docs/contract negative tests для forbidden Phase 4 scope и
  несовместимых contract versions.

### Не входит

Runtime implementation, manifests существующих apps, generator и migration.

### Exit gate

Normative документы согласованы с roadmap; baseline воспроизводим; budgets и
layer policy machine-checkable; firmware behavior не изменён.

---

## 3.2 — Native app manifest schema и validator

Статус пакета: `not_started`.

### Depends on

`3.1`.

### Цель

Сделать `app.toml` строгим build-time контрактом без runtime parsing.

### Scope

- [ ] Определить schema 1: `id`, `name`, `version`, `entry`, lifecycle kind,
  source/include declarations, menu metadata, stable autostart ID, required и
  optional capabilities, app-scoped services, limits и test metadata.
- [ ] Использовать canonical app ID rules и запретить collision по ID, entry,
  autostart ID, menu order и generated symbol.
- [ ] Версии capabilities задавать minimum/maximum-exclusive и required feature
  set, совместимо с Phase 2 inventory semantics.
- [ ] Optional requirements обязаны иметь декларативный fallback.
- [ ] Limits описывают static RAM, stack expectation, tick/render budget и app
  state size; нулевые/отрицательные/unbounded значения запрещены.
- [ ] Paths разрешаются относительно app directory, не могут покинуть его через
  `..`, symlink или case/path trick.
- [ ] Validator отклоняет unknown fields и не делает silent defaults для
  identity, capability или resource policy.
- [ ] Добавить positive fixtures и exhaustive negative schema/path/version tests.

### Exit gate

Одна команда валидирует все manifests и детерминированно выдаёт одинаковый
canonical model; malformed manifest падает до compile.

---

## 3.3 — Manifest-driven build composition

Статус пакета: `not_started`.

### Depends on

`3.2`.

### Цель

Убрать ручное дублирование app sources и capability requirements из build
tooling.

### Scope

- [ ] Добавить manifests для всех текущих feature apps с сохранением stable IDs,
  menu order, autostart IDs и текущих enable/disable defaults.
- [ ] Генерировать app source/include set и `HK_ENABLE_APP_*` из manifests.
- [ ] Перенести данные `firmware/app_requirements.toml` в manifests и удалить его
  как production source of truth.
- [ ] Capability composition должен читать тот же canonical manifest model, а не
  собственную копию schema.
- [ ] Сохранить `--disable-app` и `--require-app`; добавить понятные diagnostics
  для absent required capability/service.
- [ ] Доказывать полное исчезновение disabled app sources, private symbols,
  third-party dependencies и resource sections.
- [ ] Добавить generated-file freshness и deterministic-order checks.

### Exit gate

Full и representative disabled builds совпадают по behavior с Phase 2; app
composition имеет один source of truth; ручное добавление app в build script не
нужно.

---

## 3.4 — Generated registry и legacy adapter

Статус пакета: `not_started`.

### Depends on

`3.3`.

### Цель

Заменить ручную registry table generated descriptors, не заставляя сразу
переписывать двенадцать apps.

### Scope

- [ ] Генерировать immutable registry descriptors из canonical manifests.
- [ ] Генерировать menu title/order, stable autostart lookup, help/debug metadata,
  tick policy и capability/service requests.
- [ ] Оставить маленький generic registry runtime для lookup/iteration/dispatch.
- [ ] Добавить explicit `legacy` entry adapter для ещё не мигрированных apps.
- [ ] Legacy callback symbols указываются manifest entry metadata и не копируются
  вручную в central C table.
- [ ] Удалить manual descriptor table из `apps/app_registry.c`; generated code не
  должен содержать board/HAL/driver policy.
- [ ] Сохранить persisted autostart compatibility независимо от menu order.
- [ ] Проверить empty registry, single app, all apps, disabled app и mixed
  legacy/v2 compositions.

### Exit gate

Добавление fixture app manifest не меняет central registry source; menu,
autostart, debug dispatch и current apps работают как до migration.

---

## 3.5 — App Runtime v2 lifecycle engine

Статус пакета: `not_started`.

### Depends on

`3.1`, `3.4`.

### Цель

Реализовать portable fixed-capacity lifecycle state machine без подключения её к
production menu.

### Scope

- [ ] Реализовать states `inactive`, `injecting`, `probed`, `prepared`, `running`,
  `stopping`, `cleaning`, `faulted`. Provider quarantine остаётся отдельным
  состоянием capability provider и не используется как app-runtime state.
- [ ] Реализовать callbacks `probe`, `prepare`, `start`, `event`, `tick`, `render`,
  `stop`, `cleanup` в нормативном порядке.
- [ ] `prepare` failure вызывает полный unwind; `start` не вызывается без
  successful injection/probe/prepare.
- [ ] `stop` идемпотентен; app `cleanup` выполняется не более одного раза для
  normal close, BACK, timeout, exception/failure и forced switch.
- [ ] Реализовать точный teardown: `stop(ctx)` → app `cleanup(ctx)` при валидных
  declared handles → обязательный runtime owner-wide capability/service cleanup
  → provider quarantine по Phase 2 при failed provider cleanup → invalidate
  owner handles → invalidate context generation → разрешить state reuse.
- [ ] Ошибка app cleanup не останавливает runtime owner-wide cleanup; deferred
  work использует generation/epoch token и не влияет на следующий run.
- [ ] Reentrant switch/callback и callback после context generation invalidation
  возвращают deterministic error.
- [ ] App state остаётся private fixed storage; descriptor size/alignment
  проверяются против manifest limits.
- [ ] Добавить host fake apps, fault injection на каждом transition и transition
  table tests.
- [ ] Проверить отсутствие heap/tasks/queues и bounded transition latency.

### Exit gate

Lifecycle normative suite полностью проходит на host; production app dispatch
ещё не изменён.

---

## 3.6 — Public app context и capability injection

Статус пакета: `not_started`.

### Depends on

`3.5`.

### Цель

Выдать v2 app только объявленные handles с единым owner lifetime.

### Scope

- [ ] Определить public `hk_app_context_t` в SDK без platform/private pointers.
- [ ] Context содержит app identity, generation, owner, declared required/optional
  capability handles и app-scoped service handles.
- [ ] Required grants разрешаются до `probe`; missing/incompatible grant не
  запускает app.
- [ ] Optional grant либо присутствует, либо возвращает manifest fallback state.
- [ ] App не может запросить capability, не объявленную manifest.
- [ ] Все acquired handles принадлежат одному runtime owner. Они остаются
  валидными во время app cleanup, затем runtime выполняет owner-wide cleanup и
  только после этого инвалидирует handles и context generation.
- [ ] Failure app cleanup не пропускает owner-wide cleanup; provider quarantine
  применяется только если cleanup соответствующего provider не смог установить
  safe state. Stale copied context/handle не получает доступ при следующем run.
- [ ] Добавить fake inventory/grant tests, version/feature mismatch, partial
  injection unwind и owner exhaustion tests.

### Exit gate

Host app получает ровно manifest-declared surface; negative architecture and
stale-handle tests зелёные; Phase 2 providers не меняют semantics.

---

## 3.7 — Event, tick, render и switching integration

Статус пакета: `not_started`.

### Depends on

`3.6`.

### Цель

Подключить runtime v2 к firmware loop, сохранив menu и legacy apps через adapters.

### Scope

- [ ] Определить bounded SDK events для input, SD/media change, timer, runtime
  close и app-private wakeup token без public `screen_t`.
- [ ] Перевести input snapshot в ordered app events через existing Input
  capability; не создавать вторую button path.
- [ ] Runtime планирует tick по manifest interval/budget и передаёт monotonic now.
- [ ] Render получает SDK surface/view contract поверх injected Display handle;
  app запрашивает invalidation, а не управляет LCD ownership.
- [ ] Menu open, BACK, autostart, debug forced switch и safe-mode выполняют один
  switch/unwind algorithm.
- [ ] Legacy adapter сохраняет current behavior, но проходит через общий switch
  boundary и owner cleanup.
- [ ] Deferred work использует generation token; старый app не рисует, не пишет
  state и не завершает cleanup нового app.
- [ ] Добавить rapid-switch, BACK-during-start, timeout, render failure, autostart
  fallback и mixed legacy/v2 tests.

### Exit gate

Mixed runtime работает в full firmware host harness; current physical behavior
не требует проверки до первой migrated app.

---

## 3.8 — Feature App SDK core и host fake

Статус пакета: `not_started`.

### Depends on

`3.6`, `3.7`.

### Цель

Опубликовать минимальный portable SDK, которым реально можно собрать app отдельно
от firmware.

### Scope

- [ ] Создать `sdk/include` как public entry surface Feature App SDK.
- [ ] SDK headers могут включать/re-export только public Phase 2 Capability API
  types из `firmware/include/hackylens/capability/`; не создавать redundant
  app-specific wrappers для Time/Display/Input без доказанной ABI-причины.
- [ ] Экспортировать app definition/context, lifecycle ops, events, result/error,
  deadline/cancel и declared capability handles/types.
- [ ] Не реэкспортировать board, HAL, SDK, drivers, private capability providers,
  raw SD или framebuffer ownership internals.
- [ ] Добавить CMake/make integration для standalone app compile.
- [ ] Добавить deterministic host fake platform: time, input, display, grants,
  lifecycle driver и failure injection.
- [ ] Добавить compatibility metadata и compile tests для C/C++ consumers.
- [ ] Architecture guard запрещает SDK→platform/app-private и app→anything кроме
  SDK plus own private headers; разрешено только SDK public header→public
  Capability API header, но не implementation/provider/private headers.

### Exit gate

Минимальный fixture app компилируется и тестируется только через SDK/fakes; SDK
header closure не содержит repository-private dependencies.

---

## 3.9 — `new_app.py` и independent sample app

Статус пакета: `not_started`.

### Depends on

`3.2`–`3.8`.

### Цель

Доказать эргономику SDK до добавления storage/camera complexity и до миграции
production features.

### Scope

- [ ] Реализовать `python tools/new_app.py <id>` с безопасным отказом при
  collision/overwrite.
- [ ] Генерировать directory, manifest, public entry, private state/controller/
  view split, lifecycle ops и host tests.
- [ ] Capability declarations выбираются flags generator, а не редактированием
  central templates после generation.
- [ ] Generated code deterministic, format-clean и architecture-valid.
- [ ] Создать простой sample app только на Time/Input/Display без platform
  includes, logging dependency и ручной правки core/registry/build script.
- [ ] Проверить standalone fake build, full firmware inclusion, disable/exclusion
  и resource report.
- [ ] Добавить CLI negative tests для invalid ID, unknown capability, existing
  directory и unwritable target.

### Exit gate

Новый sample app создаётся одной командой и проходит tests/build только через
manifest + SDK; central firmware files не меняются для регистрации app, а
services следующих пакетов ему не требуются.

---

## 3.10 — App-scoped Settings и Logging

Статус пакета: `not_started`.

### Depends on

`3.9`.

### Цель

Дать app изолированные settings и bounded diagnostics без прямого доступа к
settings storage или debug transport.

### Scope

- [ ] Определить versioned settings namespace с app ID isolation, fixed-size
  records, validation, atomic commit и compatibility rules.
- [ ] Определить structured bounded logging без format heap allocation и без
  прямого UART/debug driver access.
- [ ] Manifest объявляет используемые settings/logging namespaces; runtime
  injects only declared service handles.
- [ ] Реализовать portable service boundaries поверх existing settings/debug
  services без дублирования production implementation.
- [ ] Добавить shared fake/production normative cases для namespace isolation,
  record validation, atomic settings commit, bounded logging и cleanup.

### Не входит

Storage paths/quotas, USERFS project format, package installation и Phase 4
workflow.

### Exit gate

App не может читать/портить чужие settings или писать напрямую в UART/debug
driver; fake и production adapters проходят одинаковые normative cases.

---

## 3.11 — App-scoped Storage

Статус пакета: `not_started`.

### Depends on

`3.10`.

### Цель

Дать app bounded filesystem service с явными namespaces и permissions без raw SD
или произвольных platform paths.

### Scope

- [ ] Определить app-private storage namespace и явно разрешённые shared-media
  operations; raw SD blocks и arbitrary platform paths запрещены.
- [ ] Защитить resolution от `..`, absolute path, symlink, case-folding и других
  path escape/collision вариантов.
- [ ] Manifest объявляет storage namespaces, permissions и fixed quotas; runtime
  injects only declared service handles.
- [ ] Reads/writes bounded, deadline/cancel aware и не загружают file целиком без
  declared limit.
- [ ] Определить atomic replace/commit и deterministic partial-write/power-loss
  semantics.
- [ ] Реализовать portable boundary поверх existing storage layer без raw block
  или app-specific filesystem bypass.
- [ ] Добавить shared fake/production normative cases для isolation, quota, path
  escape, cancellation, partial write, recovery и cleanup.

### Не входит

USERFS project format, package installation, editor/upload workflow, Files-app
migration и Phase 4 workflow.

### Exit gate

Fixture apps не могут выйти из разрешённого namespace, обойти quota или получить
raw SD access; fake и production adapters проходят одинаковые normative cases.

---

## 3.12 — Camera/frame public contract и deterministic fake

Статус пакета: `not_started`.

### Depends on

`3.8`.

### Цель

Зафиксировать portable camera/frame semantics до K210 production migration.

### Scope

- [ ] Определить `hackylens.cap.camera` `0.1.0 experimental`: configuration,
  start/stop, bounded capture/poll, frame metadata, deadline/cancel и cleanup.
- [ ] Определить borrowed frame ownership, generation, immutable-until-release,
  format/stride/geometry и stale-token semantics.
- [ ] Определить fixed-capacity limits, one-shot completion/error behavior и
  teardown interaction с app owner lifecycle.
- [ ] Реализовать deterministic allocation-free host fake с fault injection.
- [ ] Создать provider-independent normative host suite для configuration,
  start/stop, capture/poll, cancellation, deadlines, release, stale tokens и
  cleanup.
- [ ] Зафиксировать public header/ABI closure без K210/DVP/driver dependencies.

### Не входит

K210 provider, production consumer migration, Vision, QR и AI APIs.

### Exit gate

Public contract и deterministic fake полностью проходят normative host suite;
production firmware camera path не изменён.

---

## 3.13 — K210 camera provider и consumer convergence

Статус пакета: `not_started`.

### Depends on

`3.12`.

### Цель

Реализовать Camera capability на существующем K210 path и убрать параллельные
native/MicroPython hardware paths.

### Scope

- [ ] Переиспользовать existing DVP, camera stream и frame pool; второй
  framebuffer или frame queue запрещены.
- [ ] Сохранить camera reservation и scratch workspace mutual exclusion.
- [ ] K210 provider и fake проходят одну provider-independent normative suite;
  adapter-specific ISR/DVP cases остаются supplemental.
- [ ] Свести существующих native и MicroPython v1 camera consumers к одному
  provider boundary без изменения MicroPython API v1.
- [ ] После migration удалить второй production camera hardware path.
- [ ] Добавить architecture guards против app/adapter→DVP/camera driver bypass.
- [ ] Зафиксировать flash/static RAM и frame-latency evidence.

### Не входит

Vision, QR, AI APIs и lifecycle v2 migration конкретного CAMERA app.

### Exit gate

Fake и K210 provider имеют одинаковую lifecycle/frame semantics; firmware имеет
один production camera hardware path; current consumers и both profiles зелёные.

---

## 3.14 — BUTTONS migration proof

Статус пакета: `not_started`.

### Depends on

`3.9`.

### Цель

Первым production app доказать простую event/render migration.

### Scope

- [ ] Перевести BUTTONS manifest из legacy entry в lifecycle v2.
- [ ] Private state живёт в app module; runtime передаёт Input/Display через
  context и SDK events/surface.
- [ ] Удалить legacy callbacks/registry metadata, которые больше не используются.
- [ ] Сохранить текущий BUTTON TEST UI, press/hold/release/no-repeat behavior и
  выбранный размер шрифта.
- [ ] Добавить golden behavior и draw-region tests до/после migration.
- [ ] Проверить enter/BACK/forced switch/cleanup/re-entry и disabled build.
- [ ] На устройстве повторить только BUTTONS + menu switch smoke.

### Exit gate

BUTTONS работает только как v2 app; legacy path отсутствует; automated gate и
targeted owner smoke зелёные. Camera, storage и settings packages для закрытия
BUTTONS не требуются.

---

## 3.15 — PONG migration proof

Статус пакета: `not_started`.

### Depends on

`3.14`.

### Цель

Доказать stateful fixed-step app с tick budget и dirty rendering.

### Scope

- [ ] Перевести PONG на lifecycle v2 и injected Time/Input/Display.
- [ ] Fixed-step accumulator, serve state, collision semantics и dirty regions
  не меняются без отдельного normative reason.
- [ ] Tick/render budget из manifest проверяется host harness и resource report.
- [ ] STOP/BACK/forced switch очищают state; re-entry начинает deterministic
  session без stale tick/render.
- [ ] Сохранить existing golden physics и display regression tests.
- [ ] На устройстве повторить только launch/gameplay/BACK/re-entry.

### Exit gate

PONG не использует legacy lifecycle и сохраняет Phase 2 gameplay/render
behavior; targeted owner smoke и CI зелёные. Camera/storage packages не являются
его dependency.

---

## 3.16 — CAMERA migration proof

Статус пакета: `not_started`.

### Depends on

`3.10`, `3.11`, `3.13`, `3.15`.

### Цель

Выполнить обязательный camera app proof через injected capabilities и services
после BUTTONS и PONG.

### Scope

- [ ] Перевести только CAMERA app, не QR/FACE/APRILTAG/OBJECT, на lifecycle v2.
- [ ] Использовать injected Camera, Display, Input, Time, Lights, settings,
  logging и storage handles; app не включает camera stream, frame pool, raw SD
  или HAL.
- [ ] Camera settings child session становится app-private state, а не отдельной
  public screen identity.
- [ ] Photo write использует SDK storage namespace и сохраняет current format/path
  compatibility либо документированную migration.
- [ ] Stop/cleanup корректны для active frame, capture, settings, photo write,
  BACK, timeout и forced switch.
- [ ] Deferred provider work использует generation/epoch token и не трогает state
  после exit/re-entry.
- [ ] Сохранить camera FPS/visual behavior и full-frame display path в пределах
  Phase 3 budgets.
- [ ] На устройстве проверить только camera start/live view/settings/photo/BACK/
  re-entry и быстрый switch.

### Exit gate

CAMERA работает как v2 app только через SDK/injected providers; driver/private
bypasses отсутствуют; targeted physical smoke и full CI зелёные.

---

## 3.17 — Qualification, versions и Phase 3 closure

Статус пакета: `not_started`.

### Depends on

`3.1`–`3.16`.

### Цель

Закрыть Phase 3 одной проверяемой цепочкой без overclaim migration или hardware
portability.

### Scope

- [ ] Зафиксировать App Runtime, App Manifest и App SDK `0.1.0 experimental`.
- [ ] Зафиксировать firmware `0.5.0` и обновить canonical version sources.
- [ ] Принять Phase 3 ADR statuses и добавить contracts в normative spec index.
- [ ] Прогнать manifest/generator/registry freshness и all host tests.
- [ ] Прогнать standalone SDK/sample build, full SEN0305, representative
  disabled-app profiles и Maix Cube compile conformance.
- [ ] Проверить architecture source/dependency/object-symbol graph без app-specific
  allowlists.
- [ ] Проверить absence старых BUTTONS/PONG/CAMERA lifecycle и hardware bypasses.
- [ ] Проверить resource budgets и bounded lifecycle/event/tick/render timings.
- [ ] Сформировать immutable automated result, exact implementation commit/CI и
  cumulative impact-based SEN0305 ledger.
- [ ] Физически не повторять UART/I2C/Lights/MicroPython/Files и другие
  незатронутые Phase 2 tests. Closure smoke содержит только runtime switching,
  BUTTONS, PONG, CAMERA, autostart/safe-mode и boot sanity.
- [ ] Обновить `CURRENT_STATE.md`, `MODULES.md` и Phase 3 status в `ROADMAP.md`.

### Machine-checkable final gate

- [ ] Manifest — единственный app composition source.
- [ ] Generated registry детерминирован и не содержит persistent identity по
  array/link order.
- [ ] New sample app не требует изменений core/registry/build script.
- [ ] Required/optional grants и app-scoped services следуют manifest.
- [ ] Lifecycle failure/switch/cleanup/stale-callback suite зелёный.
- [ ] SDK header closure portable; app→board/HAL/platform-SDK/drivers edges равны
  нулю.
- [ ] BUTTONS, PONG и CAMERA используют lifecycle v2; остальные legacy apps явно
  изолированы adapter-ом.
- [ ] Camera имеет один production provider path и сохраняет frame ownership.
- [ ] Full/disabled/Cube builds и resource evidence зелёные.
- [ ] Physical evidence привязано к exact SEN0305 image и не квалифицирует Cube.

### Exit gate

Phase 3 получает `completed` только после green exact-commit CI и достаточного
targeted owner smoke. Непереведённые legacy apps и отложенные Phase 4+ функции
перечисляются явно и не маскируются как выполненные.

## Как продолжать работу

Следующий implementation turn называется номером execution-пакета, например
`Phase 3.1`, `Phase 3.9` или `Phase 3.13`. Перед изменением source нужно
прочитать этот пакет, его dependencies и связанные normative contracts. Итог
turn сообщает:

1. какие checklist items завершены;
2. какие contracts/files изменены;
3. какие tests/builds прошли;
4. resource/latency delta для runtime changes;
5. нужен ли targeted hardware smoke;
6. можно ли поставить всему пакету `[x]`.
