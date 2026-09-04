# HackyLens Simplification Masterplan

## Статус и назначение

Статус: `planned`.

Исходная ревизия:
`71ec63fffe2926642fba00ff8d28519fd93750d9` (`phase-3-work`, Phase 3.8
completed).

Этот документ — временный execution plan по уменьшению сложности HackyLens.
Он не является новой нормативной системой, не требует отдельного ADR, evidence
schema или checker и может быть удалён после завершения работы. Источниками
истины остаются observable firmware behavior, public API headers, build inputs
и выполняемые tests.

Пакеты Phase 3.1–3.8 остаются исторически завершёнными. Исполнение Phase
3.9–3.17 в текущем виде приостановлено: generator и новые SDK contracts нельзя
строить поверх архитектуры, которую этот план должен упростить. Phase 4+ не
начинается.

## Цель

Сделать HackyLens меньше и понятнее без потери того, ради чего проект
существует:

- рабочей SEN0305/K210 firmware;
- board-independent feature apps;
- одного hardware implementation для native apps и MicroPython;
- build-time app composition без runtime TOML parser и dynamic loading;
- bounded embedded runtime без случайного heap, новых общих tasks, queues,
  cores и framebuffers;
- совместимости MicroPython API v1 и HMPY;
- воспроизводимой сборки, измеримых flash/static RAM и impact-based hardware
  testing.

Предпочтение при любой реализации:

`DELETE > MERGE > INLINE > REUSE > MODIFY > ADD`.

Новая abstraction допустима только если без неё ломается текущий реальный use
case и если она заменяет больше surface area, чем добавляет.

## Исходный масштаб

Приблизительная audit baseline без third-party:

| Область | Значение |
| --- | ---: |
| Production C/C++ | 42,593 LOC |
| C/C++ tests | 16,090 LOC |
| Python tooling | 17,840 LOC |
| Python tests | 9,655 LOC |
| Documentation | 9,720 LOC |
| Specialized checkers | 15 / около 7,600 LOC |
| Python test cases | 280 |
| C harness/suite/backend/stub files | 43 |
| Normative specs | 13 |
| ADR | 8 |
| Evidence artifacts | 14 |
| Production app manifests | 12, по 26 полей |

Support/infrastructure LOC уже примерно в 1.25 раза больше production LOC.
Эти числа являются ориентиром, а не обязательным percentage gate. Нельзя
заменять complexity одним новым checker, измеряющим complexity.

## Обязательные инварианты на время перехода

1. `main` не изменяется; работа идёт в `phase-3-work` или в отдельно
   согласованной ветке.
2. После каждого substantial change firmware остаётся buildable и usable на
   SEN0305.
3. Apps не получают pins, HAL, driver/provider private headers или raw SD
   access.
4. Manifest остаётся build-time-only. Runtime discovery/registration, TOML
   parser и dynamic loading не добавляются.
5. Один foreground app. Существующий core1 executor остаётся единственным
   shared background execution mechanism, пока реальная feature functionality
   от него зависит.
6. Реальные asynchronous paths сохраняют stale-result protection: core1 ticket,
   detector session epoch, external-link operation generation и frame borrow
   generation удаляются только вместе с состоянием, которое они защищают.
7. Один absolute monotonic teardown deadline создаётся при начале teardown и не
   обновляется между app stop и service/provider cleanup. Ошибка app stop не
   пропускает cleanup.
8. Provider quarantine сохраняется там, где failed cleanup реально может
   оставить hardware в небезопасном состоянии, пока не появится более простой
   локальный эквивалент.
9. Stable app IDs, autostart IDs, Settings persistence и menu behavior не
   меняются случайно при упрощении manifests/registry.
10. MicroPython API v1, HMPY wire behavior и единственный production camera path
    сохраняются.
11. Physical tests повторяются только для затронутых hardware/UI/runtime paths.
12. Status меняется на `completed` только после green normal-push CI текущего
    HEAD. Workflow dispatch не заменяет push CI.

## Порядок выполнения

Фазы выполняются строго последовательно. Внутри фазы разрешены несколько
небольших commits, но не обязательны implementation/closure pairs. Следующая
фаза не начинается до выполнения exit gate предыдущей.

Статусы:

- `[ ]` — не начато;
- `[~]` — в работе;
- `[x]` — exit gate выполнен и normal-push CI зелёный.

---

## S1 — Удаление governance и CI ceremony

Риск: **низкий**. Production source не изменяется.

Статус: `[x]`.

### Цель

Сначала удалить machinery, которая проверяет документы, receipts и workflow,
а не firmware behavior.

### Scope

- Убрать tests конкретных Markdown-фраз, test names, helper names и literal
  workflow expressions.
- Сократить documentation validation до broken local links и действительно
  публичных version constants; не вводить новый docs checker.
- Удалить phase-specific baseline/result/candidate/closure validation из
  normal-push CI.
- Сохранить один понятный current SEN0305 hardware acceptance summary; старые
  JSON chains и дублирующие Phase reports оставить в Git history, а не в active
  documentation.
- Убрать повторный `run_phase2_contracts.py`, если его cases уже запускает
  основной test runner.
- Сократить normal-push profiles: full SEN0305 обязателен; MicroPython-disabled,
  capability-absent и Cube запускаются только при relevant changes, nightly или
  release.
- Не менять runtime, SDK, manifests, board descriptors или firmware behavior.

### Основные кандидаты

- `tools/check_docs.py` и `tests/test_docs_contracts.py`;
- `tests/test_ci_contracts.py`;
- `tools/check_phase2_evidence.py`, `check_phase2_exit.py` и соответствующие
  tests;
- phase-specific receipt paths в `.github/workflows/release.yml`;
- дублирующие `docs/evidence/phase*-result*.json` и closure reports.

### Exit gate

- Ни один production C/C++ file не изменён.
- Основной host suite, full SEN0305 и один контрольный MicroPython-disabled build
  проходят.
- Release/tag packaging path остаётся рабочим.
- Normal-push CI зелёный и больше не валидирует собственное наличие через
  `test_ci_contracts.py`.
- Hardware test не нужен.

### Rollback

Один revert документационных/CI commits; firmware artifacts не затрагиваются.

---

## S2 — Консолидация test pyramid

Риск: **низкий–средний**. Production behavior не меняется, но меняется способ
его проверки.

Статус: `[x]`.

### Цель

Оставить tests, ловящие разные классы реальных failures, и удалить
пересекающиеся wrappers/source assertions.

### Scope

- Разделить suite на четыре понятных уровня: unit, service contracts, один
  runtime integration path, firmware build/hardware smoke.
- Удалить Python assertions, которые проверяют spelling C implementation вместо
  поведения.
- Shared fake/K210 service cases сохранить, но не повторять их через docs,
  architecture text и отдельный receipt runner.
- Объединять C harnesses только там, где это уменьшает setup и не создаёт
  artificial symbol/config complexity.
- Сохранить HMPY golden vectors, MicroPython compatibility, settings/storage,
  codecs, app logic и K210 adapter behavior tests.
- Runtime v2/host fake tests пока не удалять: они являются safety net до S5.

### Exit gate

- Каждому оставшемуся test level соответствует уникальный failure class.
- Удалены tests-of-tests и workflow/prose tests.
- Все прежние observable behavior cases либо остаются, либо покрываются более
  прямым executable test.
- Full host suite и firmware builds из S1 проходят в normal-push CI.
- Production binary/resource delta равен нулю.
- Hardware test не нужен.

### Rollback

Revert test-only commits; production source не затронут.

---

## S3 — Минимальный app manifest и build registry

Риск: **средний**. Меняется build composition, но не app/runtime behavior.

Статус: `[x]`.

### Цель

Оставить в `app.toml` только реальные build/menu/runtime facts и одну
build-time generation path.

### Целевой manifest

```toml
id = "pong"
name = "PONG"
sources = ["pong_app.c", "pong_controller.c", "pong_view.c"]
requires = ["display", "input", "time"]
menu_order = 90
autostart_id = 8
tick_ms = 20
```

`entry` может остаться только если безопасная convention из `id` невозможна.
`generated_symbol` отдельно не нужен.

### Scope

- Удалить lifecycle selector после выбора единственного app model либо оставить
  временно только на период legacy migration.
- Удалить provider version ranges, feature arrays, service namespaces,
  placeholder static/stack/state/render budgets, help/debug prose и test build
  metadata.
- Сохранить exact source list, stable ID, menu order, autostart identity,
  required service presence и tick period.
- Парсить manifests один раз за build.
- Генерировать компактный registry в build directory, а не коммитить большой
  `firmware/generated/app_registry/registry.c`.
- Удалить tracked app composition JSON и generated defaults как отдельные
  sources of truth.
- Не добавлять runtime TOML parser.

### Exit gate

- Все 12 app IDs, menu order, hidden/visible state и stable autostart IDs точно
  совпадают с исходной ревизией.
- Full и MicroPython-disabled firmware содержат ожидаемые app symbols и не
  содержат disabled app sources.
- Settings v5 compatibility и autostart persistence проходят.
- Build output содержит один immutable registry; committed generated copy и
  composition JSON больше не нужны.
- Flash/static RAM не растут; dispatch latency не ухудшается.
- Hardware test нужен только если menu/autostart binary behavior фактически
  изменился.

### Rollback

Старые manifests и generated registry восстанавливаются одним revert; Settings
storage format не меняется.

---

## S4 — Упрощение board/build tooling

Риск: **средний**. Pins и runtime hardware behavior должны остаться неизменными.

Статус: `[x]`.

### Цель

Сохранить полезный board descriptor, но удалить universal schema machinery и
четыре tracked generated headers на каждую плату.

### Scope

- Сохранить `board.toml` как общий input для build, packaging и `hkflash`.
- Сократить schema до реально используемых pins/routes, numeric defaults,
  available services, flash layout и programming metadata.
- Генерировать один private `board_config.h` в build directory.
- Удалить tracked `pins.h`, `defaults.h`, `inventory.h`, `flash_layout.h` после
  перехода всех consumers.
- Сократить `board_contract.py`, `gen_board.py`, `gen_flash_layout.py` и
  `test_board_ports.py` до parsing/collision checks и настоящих builds.
- Сохранить `hk_board_ops` и разделение BSP/K210 HAL/drivers.
- Cube остаётся compile-conformance target, не runtime-qualified board.

### Exit gate

- SEN0305 pin/function/default/flash constants совпадают с исходными значениями.
- Full SEN0305 firmware и Cube conformance build проходят.
- `hkflash` dry-run и release packaging используют тот же flash/programming
  metadata.
- Apps по-прежнему не содержат board/HAL/driver dependencies.
- Если generated constants идентичны, hardware test не нужен. При любом pin,
  clock или peripheral setup diff обязательны только затронутые boot/display/
  input/camera/storage/external-link scenarios.

### Rollback

Вернуть старый generator/output headers; `board.toml` остаётся совместимым до
завершения фазы.

---

## S5 — Один runtime semantics на production и host

Риск: **средний–высокий**. Цель — убрать duplicate host state machine без
изменения public lifecycle.

Статус: `[x]`.

### Цель

Host tests должны исполнять production runtime core, а не отдельную реализацию
его semantics.

### Scope

- Сделать production app runtime host-compilable через существующие public
  types и test implementations services/providers.
- Перенести high-value Phase 3.8 cases на реальный runtime: lifecycle failures,
  Input overflow, Time limits, Display transactions, stale generation,
  teardown deadline и owner cleanup.
- App logic tests могут использовать маленькие typed service doubles, но не
  второй lifecycle engine.
- После эквивалентного coverage удалить `sdk/host/src/host_fake.c`, public
  `host_fake.h`, AppHostFake build target и fake-specific fixture machinery.
- Public `hackylens/app.h` может остаться; новый host framework не создаётся.
- Runtime lifecycle callbacks в этой фазе ещё не сокращать: это scope S6.

### Exit gate

- Каждый сохранённый lifecycle/capability case исполняет production runtime
  core.
- В repository остаётся ровно одна lifecycle state machine.
- Standalone sample либо тестируется с real runtime library, либо удаляется как
  неиспользуемый product surface.
- Full firmware resource/latency delta нулевой или отрицательный.
- Normal-push CI зелёный.
- Если production runtime source изменился только для host composition и
  firmware object code идентичен, hardware test не нужен; иначе проверить app
  open/BACK/switch/failure fallback.

### Rollback

До удаления fake его tests должны проходить параллельно real-runtime tests.
Удаление выполняется отдельным легко обратимым commit.

---

## S6 — Минимальный app lifecycle и первая production migration

Риск: **высокий**. Начинаются production runtime changes.

Статус: `[~]`.

### Цель

Заменить восьмиcallback lifecycle минимальным API, доказанным реальной app.

### Target

Базовая модель:

```text
start(ctx) -> event(ctx, event) -> stop(ctx, teardown_deadline)
```

Input, tick, media и deferred completion являются event kinds. Отдельный render
callback остаётся только если BUTTONS/PONG migration докажет, что без него
теряется полезная runtime-owned Display transaction guarantee.

### Scope

- Удалить `probe`: required services проверяются build-time и при создании
  context.
- Слить `prepare` со `start`.
- Слить app `cleanup` со `stop`; runtime-owned service/provider cleanup всегда
  выполняется после него.
- Сократить runtime states до минимально нужных inactive/active/stopping и
  failure bookkeeping.
- Сохранить один teardown deadline без refresh.
- Оставить один app generation только для реально deferred work.
- Не создавать параллельный Runtime v3. Рефакторить существующий runtime in
  place и держать один временный legacy adapter.
- Сначала мигрировать BUTTONS; затем PONG как proof tick/state/render behavior.
- Выбрать direct typed services или один маленький immutable service bundle.
  Bundle нельзя добавлять, если global typed APIs дают тот же portability/test
  seam проще.

### Exit gate

- BUTTONS и PONG работают на новом lifecycle; остальные apps продолжают
  работать через один legacy adapter.
- Rapid switch, BACK during start/run, callback failure, teardown timeout и
  stale deferred event проходят на production runtime host suite.
- Full firmware, MicroPython-disabled profile и resource checks проходят.
- Новых heap/task/queue/core/framebuffer sites нет.
- Измерены flash, static RAM и dispatch latency относительно исходной ревизии.
- Targeted SEN0305 test: menu → BUTTONS → BACK, menu → PONG → play/input → BACK,
  repeated switching и failure fallback при доступном injection seam.
- Normal-push CI зелёный.

### Rollback

Legacy adapter остаётся до завершения S7. При regression BUTTONS/PONG можно
временно вернуть на legacy entry без изменения persistent identity.

---

## S7 — Миграция production apps и удаление legacy runtime

Риск: **высокий**. Затрагиваются все feature paths, но не одновременно.

Статус: `[ ]`.

### Цель

Перевести существующие apps на один lifecycle и удалить hidden global app
callbacks.

### Migration waves

1. Простые UI apps: Terminal, Settings, Sleep.
2. Files и QR Camera.
3. Camera, Face Detect, Object Detect, AprilTag.
4. MicroPython последним, сохраняя API v1, WDT recovery и HMPY.

### Scope

- Мигрировать по одной app или тесно связанной паре; после каждой wave firmware
  остаётся usable.
- `background_tick` заменить explicit service polling/completion paths. Inactive
  app не получает скрытые callbacks.
- `handle_sd_event`, debug commands и secondary screens перевести в обычные
  events/services без раскрытия board/platform details.
- Сохранить detector epochs, core1 tickets и buffer generations, пока jobs могут
  пережить foreground callback.
- После последней app удалить lifecycle selector, legacy entry union,
  `screen_t` как app identity и legacy adapter branches.
- Не менять camera hardware implementation и не создавать второй provider path.

### Exit gate

- Все 12 apps используют один lifecycle; registry больше не содержит `legacy`.
- Нет global registry iteration, вызывающей callbacks inactive apps.
- MicroPython API v1/HMPY, autostart, Settings, Files, camera/AI и debug paths
  проходят соответствующие host tests.
- После каждой wave выполнен impact-based SEN0305 test только затронутых apps.
- После удаления legacy adapter измерены flash, static RAM и dispatch latency;
  net complexity и firmware resources не выросли без документированной
  product-причины.
- Normal-push CI зелёный.

### Rollback

Каждая app migration — отдельный обратимый commit. Legacy adapter удаляется
только после green CI и hardware acceptance последней wave.

---

## S8 — Typed services вместо universal Capability broker

Риск: **очень высокий**. Это последняя и наиболее опасная structural change.

Статус: `[ ]`.

### Цель

Сохранить board-independent typed hardware access, но удалить dynamic machinery,
не нужную build-time composed single-firmware product.

### Scope

- Сохранить app-facing typed Time/Input/Display/Lights/External Link semantics.
- Заменять broker за стабильной app-facing boundary по одному service:
  1. Time;
  2. Input;
  3. Lights;
  4. Display;
  5. External Link последним.
- Camera и Storage остаются typed services и не получают новый generic
  capability wrapper.
- Build/board binding предоставляет immutable handles напрямую; optional service
  — `NULL` или build-time app exclusion.
- Удалить runtime version/feature negotiation, inventory discovery, generic
  owner/grant/lease tables и per-handle generations там, где operation строго
  synchronous.
- Реальные conflicts локализовать: Display transaction, Lights channels,
  External Link exclusive mode, Camera/frame borrow, core1 executor.
- Local provider fault/quarantine сохранить для failed cleanup, способного
  оставить hardware unsafe.
- Persistent firmware consumers мигрировать явно; нельзя удалить generic owner
  core, пока хотя бы один consumer зависит от его cleanup/exclusivity.
- После последнего consumer удалить capability inventory generator, catalog
  composition, owner runtime и phase-specific contracts/tests.

### Exit gate

- Native apps и MicroPython используют одни production service implementations.
- Старые provider-independent behavioral suites проходят против новых bindings
  либо заменены более прямыми equivalent tests.
- Нет generic owner/grant/lease/inventory runtime tables и generated capability
  composition, если у них не осталось реального consumer.
- Teardown всё ещё пытается закрыть все app-scoped service sessions с одним
  исходным deadline; failure не пропускает invalidation/state retirement.
- Full/MicroPython-disabled builds, flash/static RAM и critical service latency
  измерены.
- Targeted hardware qualification выполняется после каждого изменённого service:
  Input, Lights, Display, UART/I2C, затем Camera/Files/MicroPython только если их
  binding затронут.
- Normal-push CI зелёный.

### Rollback

Старый broker остаётся backend для ещё не мигрированных services. Каждый service
switch должен быть отдельным commit; массового cutover не допускается.

---

## S9 — Финальная convergence и roadmap reset

Риск: **низкий для source, средний для qualification**.

Статус: `[ ]`.

### Цель

Удалить временные compatibility surfaces, подтвердить итоговую firmware и
вернуть roadmap к product outcomes.

### Scope

- Удалить оставшиеся legacy adapters, superseded generated artifacts, obsolete
  specs, ADR framework, masterplans и phase evidence, если они больше не нужны
  для active development.
- Объединить Architecture Vision, Architecture, Current State и оставшиеся
  полезные decisions в один current `ARCHITECTURE.md`.
- Сократить `ROADMAP.md` до ближайших product outcomes: SEN0305 features,
  physically qualified second K210 board, demand-driven MicroPython evolution.
- Project Format, Program Manager, dynamic loading, Python-to-native generator,
  IDE и ecosystem/conformance work оставить deferred до реального use case.
- Выполнить один final full validation без новой closure/evidence schema.

### Exit gate

- Один app lifecycle, одна runtime semantics, один registry source, один board
  input и один production hardware path на service.
- Full host suite, full SEN0305, MicroPython-disabled и release packaging
  проходят.
- Flash/static RAM/latency сравнены с исходной ревизией; итоговые изменения
  объясняются product functionality, а не новой governance machinery.
- Выполнен один consolidated impact-based SEN0305 pass по paths, реально
  затронутым S6–S8. Уже независимые hardware observations не повторяются.
- Normal-push CI текущего HEAD зелёный.
- Этот masterplan после завершения не становится permanent normative document.

## Ожидаемый результат, не являющийся quota

Реалистичный диапазон после S9:

- минус 20–30k handwritten LOC;
- минус 60–90 files;
- lifecycle callbacks: `8 → 3` (или 4 при доказанной отдельной render stage);
- manifest fields: `26 → 5–7`;
- lifecycle semantic implementations: `2 → 1`;
- normal-push firmware builds: `8 → 1–2`;
- normal-push checker/tool invocations: примерно `37 → 6–10`;
- для изменения обычной app нужны source, один manifest и 1–2 current docs, а
  не дерево Phase/ADR/spec/evidence contracts.

Если безопасное упрощение даёт меньший результат, цифра не является причиной
удалять полезную protection. Если новый механизм нужен только потому, что
«может пригодиться в будущем», он не входит в этот plan.
