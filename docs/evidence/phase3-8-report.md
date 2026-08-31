# Phase 3.8 completion report

Пакет: `3.8 — Feature App SDK core и host fake`

Статус: `in_progress` до зелёного normal-push Release firmware CI для exact
implementation commit.

Ветка: `phase-3-work`

## Реализованный scope

- Добавлен канонический public entry point `sdk/include/hackylens/app.h` с
  compile-time metadata SDK `0.1.0`, совместимых App Runtime/Capability API
  диапазонов и manifest schema major.
- Public lifecycle ABI (`hk_app_v2_entry_t` и callback types) принадлежит SDK;
  private runtime больше не содержит параллельную копию ABI.
- SDK переиспользует только публичные Capability API types и не раскрывает
  provider, runtime-private, board, platform, HAL или driver interfaces.
- Добавлены standalone CMake и Make integration surfaces.
- Добавлен allocation-free deterministic host fake с caller-owned fixed
  storage для Time, Input, Display, manifest-style grants, lifecycle, render,
  owner cleanup и failure injection.
- Минимальный lifecycle-v2 app собирается и исполняется только с App SDK,
  собственным private header и host fake через CMake и Make.
- C11/C++17 header consumers и recursive public-header closure проверяются
  механически.
- Architecture guard запрещает private dependencies из SDK/host fake и прямые
  Capability/private includes из lifecycle-v2 production apps.

Production app в этом пакете не мигрирован. Host fake не включён в production
firmware и не создаёт второй runtime, provider или hardware path.

## Изменённые contracts и основные файлы

- Public SDK: `sdk/include/hackylens/app.h`,
  `sdk/include/hackylens/app/runtime.h`,
  `sdk/include/hackylens/app/host_fake.h`.
- Host fake и standalone build metadata: `sdk/host/src/host_fake.c`,
  `sdk/CMakeLists.txt`, `sdk/hackylens-app-sdk.mk`.
- Runtime ABI binding и immutable descriptor alignment:
  `firmware/src/app_runtime/runtime_private.h`,
  `firmware/src/app_runtime/runtime.c`, `firmware/src/core/hk_app.h`,
  `tools/app_registry.py`, generated app registry.
- Conformance/architecture tooling: `tools/check_app_sdk.py`,
  `tools/check_arch.py`, `tools/architecture_layers.toml`, Release workflow.
- Host fixture/tests: `tests/fixtures/app_sdk`, `tests/test_app_sdk.py`, runtime,
  architecture and documentation contract tests.
- Normative documentation: `docs/spec/APP_SDK.md`, `docs/CURRENT_STATE.md`.

## Проверки и сборки

- Полный host suite: `279/279` tests passed.
- Standalone SDK conformance: CMake/Ninja build+run, Make build+run, C11 and
  C++17 consumers passed.
- Documentation, architecture, manifest composition, capability inventory and
  Board Port Contract guards passed.
- Sipeed Maix Cube compile conformance passed; это не runtime qualification
  второй платы.
- SEN0305 full firmware: passed; raw image `1,562,168 B`.
- SEN0305 MicroPython-disabled firmware: passed; raw image `1,367,928 B`.

## Resource и latency evidence

Ресурсы сравнивались с exact immutable Phase 2 closure baseline из
`docs/evidence/phase3-baseline.json`.

| Profile | Erase-rounded flash delta | Static RAM delta |
| --- | ---: | ---: |
| full | `+20,480 B` | `+2,576 B` |
| micropython-disabled | `+16,384 B` | `+2,512 B` |

Phase 3 zero-resource gate passed: новых direct heap allocation sites,
background tasks, general queues, runtime cores и full-framebuffer
allocations/expressions — `0` в каждой категории.

Representative legacy host dispatch p99: `3 ns` при лимите `100 us`. Пакет не
меняет production runtime behavior; host fake не входит в firmware image.

## Hardware impact

Targeted physical test не требуется: пакет добавляет public headers,
standalone host-test infrastructure и guards, не меняя production app/runtime
behavior или hardware path. Повтор ранее принятых физических сценариев не имеет
impact-based основания.

## CI evidence

Implementation commit и normal-push Release firmware CI будут зафиксированы
после push. Пакет остаётся `in_progress`, а scope items masterplan —
неотмеченными до зелёного exact-commit CI.
