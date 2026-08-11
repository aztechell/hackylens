# Current Project State

> HackyLens v0.2 is a layered K210 reference firmware and MicroPython technology
> preview.

## Назначение

Этот документ фиксирует состояние проекта после HackyLens v0.2 и отделяет:

- уже доказанные свойства;
- рабочие, но ограниченные прототипы;
- архитектурные разрывы;
- отсутствующие пользовательские функции;
- qualification gates, которые ещё не пройдены.

Оценка основана на текущем source tree, host tests, build contracts и выполненных
hardware acceptance runs. Наличие исходного кода само по себе не считается
доказательством production readiness.

## Итоговая оценка

HackyLens v0.2 — сильная reference firmware и MicroPython technology preview.
Проект уже имеет слои, изолированные feature directories, compile-time app
gating, аппаратные сервисы, работающий MicroPython runtime, USERFS, HMPY и
отдельную Web Serial IDE.

Однако это ещё не аппаратно-независимая application platform:

- существует только один реальный board port;
- build system не имеет обязательного `--board`;
- registry составляется вручную, а не из manifests;
- apps не получают versioned capability context;
- native apps и MicroPython используют разные entry surfaces;
- перенос Python prototype в C app не формализован;
- Python не имеет camera/vision/KPU/storage API уровня OpenMV;
- программы нельзя полноценно выбирать и управлять ими с устройства;
- IDE остаётся однопроектным/однофайловым functional prototype;
- parity с оригинальной HUSKYLENS firmware не инвентаризирован формально.

## Матрица зрелости

| Область | Состояние | Оценка |
| --- | --- | --- |
| Базовые слои firmware | Реализованы и проверяются guard | Strong foundation |
| Изоляция feature directories | Реализована для 12 apps | Working |
| Compile-time отключение apps | `--disable-app` работает | Working |
| Board portability | Только `board_hackylens`, нет `--board` | Not proven |
| Capability architecture | Есть отдельные reusable services, общего контракта нет | Partial |
| App manifests/SDK | Отсутствуют | Missing |
| MicroPython runtime | Работает на core 1 | Technology preview |
| Python hardware API | Buttons/display/lights/UART/I2C/time | Limited v1 |
| Python camera/vision/KPU | Отсутствует | Missing |
| On-device program manager | Startup/smoke run, без browser/selection | Missing product UX |
| USERFS/HMPY | Реализованы и hardware-tested | Working v1 |
| Python-to-native workflow | Не определён | Missing |
| Web IDE | Рабочий transport/editor/device files | Functional prototype |
| Original firmware parity | Несколько apps есть, полной матрицы нет | Unknown/partial |
| Multi-board conformance | Отсутствует | Missing |
| Long-run qualification | Часть gates открыта | Incomplete |

## Что уже сделано правильно

### Layered source tree

Код разделён на `core`, `runtime`, `controllers`, `services`, `storage`, `ui`,
`drivers`, `hal`, `board` и `apps`. `tools/check_arch.py` проверяет ряд направлений
зависимостей, запрещённые legacy paths, include cycles и приватность feature
headers.

Это хорошая основа для platform architecture. Её не требуется заменять; её нужно
довести до формального board/capability/app contracts.

### Feature-based applications

В `firmware/src/apps/` находятся 12 отдельных модулей:

- terminal;
- camera;
- qr_camera;
- face_detect;
- apriltag;
- object_detect;
- files;
- buttons;
- pong;
- settings;
- sleep;
- micropython.

Каждый модуль имеет публичный app header, а `app_registry.c` является единственным
местом композиции. Build manifest позволяет исключить app directory через
`--disable-app`.

Это уже соответствует feature-based направлению, но registry и зависимости
описаны вручную. Нет manifest schema, capability requirements, generation и
независимого App SDK.

### Reusable subsystems

В проекте уже присутствуют важные platform primitives:

- camera frame leases;
- shared camera sessions;
- single-owner AI model runtime;
- reusable core-1 executor;
- display shadow/overlay и bounded SPI deadlines;
- settings persistence/migrations;
- neutral vision-result transport;
- debug console service;
- internal flash validation;
- USERFS с atomic operations;
- watchdog recovery.

Они показывают, что platform-oriented decomposition практически возможна.

### Build и tests

Текущий проект имеет:

- architecture guard;
- full и feature-disabled builds;
- flash layout generation/check;
- firmware symbol gates;
- executable C harnesses для USERFS, bindings, LCD и UART RX;
- protocol/client/acceptance tests;
- CI contract tests;
- strict runner, запрещающий скрытые skips.

После v0.2 host suite содержит 57 tests. Full K210 build и физический HMPY
workflow проходили на SEN0305.

## Firmware architecture gaps

### Board layer не является полноценным BSP system

Сейчас существуют `board_hackylens.*` и `board_pins.h`, но:

- build CLI не принимает `--board`;
- нет board descriptor или capability inventory;
- flash/layout assumptions ориентированы на конкретный SEN0305;
- отдельные hardware assumptions остаются в drivers/services/config;
- нет второй платы, проверяющей границу переносимости;
- нет board conformance tests.

Следовательно, переносимость на другие K210 boards пока является целью, а не
доказанным свойством.

### App contract является callback table, а не App SDK

`hk_app_t` описывает callbacks меню и экрана, но не предоставляет:

- versioned app ABI/API;
- app context;
- capability handles;
- manifest;
- memory/time budget;
- permission/dependency declaration;
- structured event model;
- uniform prepare/start/stop/cleanup result contract;
- генератор и standalone test kit.

Приложения изолированы по файлам, но всё ещё знают конкретные shared headers и
глобальные services.

### Capability contracts не унифицированы

Некоторые services уже используют lease/owner, другие предоставляют глобальные
функции. Error models, cancellation и lifetime описаны неодинаково. Нет единого
`hk_app_context_t`, через который runtime выдаёт только доступные capabilities.

Это усложняет перенос apps между платами и не позволяет build system заранее
доказать совместимость app с board.

### Composition остаётся ручной

`apps/app_registry.c`, Python-таблицы build manifest и compile definitions должны
изменяться согласованно. Architecture guard обнаруживает часть рассинхронизаций,
но source of truth пока не единственный.

## MicroPython state

### Доказано

- официальный upstream MicroPython встроен без MaixPy;
- VM исполняется на K210 core 1;
- core 0 сохраняет управление UI, storage и bindings;
- статический GC heap и stack limit ограничены;
- STOP проверяется в VM и native iterator gateways;
- blocking bindings имеют cancellation/cleanup protocol;
- USERFS, HMPY, stdout/stderr и reconnect работают;
- startup имеет WDT recovery policy;
- запуск из sleep пробуждает устройство перед VM execution.

### Ограничения v1

Python API предоставляет buttons, time, display overlay, LED/RGB, UART и I2C.
Это доказывает end-to-end путь, но не позволяет создавать основные computer
vision приложения.

Отсутствуют:

- camera capture/frame object;
- sensor controls;
- grayscale/ROI/resize;
- image drawing поверх camera frame;
- blobs/lines/circles/statistics;
- QR/AprilTag/face/object APIs;
- generic KPU model API;
- SD/files project API;
- structured app lifecycle/events;
- capability discovery.

### Отдельный binding path

`micropython_binding_service` использует собственный opcode/RPC dispatcher. Native
apps обычно вызывают shared services напрямую. Семантика API не сформулирована
как один capability contract с двумя adapters.

Поэтому Python prototype сейчас нельзя системно перенести в native app: hardware
calls, lifecycle, state и UI приходится сопоставлять вручную.

## On-device program UX

Приложение MicroPython показывает runtime/log status и по OK запускает startup
file либо встроенный smoke script. Оно не является Program Manager.

Не хватает:

- списка программ на устройстве;
- выбора конкретного файла;
- run/stop действий для выбранного файла;
- rename/delete/startup UX;
- просмотра metadata и ошибок;
- project awareness;
- safe-mode/recovery UI;
- перехода между несколькими программами без IDE.

Таким образом утверждение «программы можно запускать с самого HackyLens» пока
выполняется только для заранее выбранного startup, а не как полноценный workflow.

## IDE state

Отдельный `hackylens-code` repository содержит:

- Monaco editor;
- Web Serial transport;
- HMPY client;
- device file list;
- upload/run/stop/status/startup/delete/format;
- runtime console;
- встроенную API documentation;
- transport/protocol/unit tests;
- GitHub Pages deployment.

Текущий UI хранит один активный source buffer и работает прежде всего с файлами
на устройстве. Не обнаружены полноценные abstractions локального workspace,
multi-file project, folders, rename, manifest editor, persistent drafts, sync
diff, examples gallery или device simulator.

Визуальная система улучшена относительно первого прототипа, но layout и flows
остаются специально написанной узкой оболочкой и ещё не достигают целостности
Pybricks Code.

## Original firmware parity

HackyLens уже реализует camera preview, QR, face detection, AprilTag, object
detection, file browser, settings и external result transport. Но отсутствие
versioned parity matrix не позволяет точно ответить, какие функции оригинальной
HUSKYLENS восстановлены полностью.

Как минимум требуют отдельного аудита или реализации:

- face recognition/enrollment, а не только face detection;
- object/line/color tracking и learning workflows;
- learned object persistence;
- exact algorithm modes и settings original firmware;
- external UART/I2C protocol compatibility;
- сохранённые algorithms/models/user data;
- update/recovery и system UX;
- edge cases конкретных hardware revisions.

До создания `ORIGINAL_FIRMWARE_PARITY.md` parity нельзя использовать как release
claim.

## Открытые qualification gates

Несмотря на успешный SEN0305 workflow, остаются:

- второй и последующие K210 board ports;
- multi-device flash geometry validation;
- physical NOR endurance и power-loss campaigns;
- WDT1 fault-injection acceptance;
- physical BACK cleanup;
- live browser Web Serial full E2E;
- 1000-cycle run/stop/reconnect stress;
- long-running camera/KPU/Python coexistence;
- memory/stack watermarks;
- compatibility and migration across future API versions.

## Главный вывод

Проект не нужно переписывать с нуля. Текущая layered/feature foundation пригодна.
Следующий этап должен превратить существующие хорошие границы в формальный
portable platform contract:

1. BSP system;
2. Capability API;
3. App Runtime/SDK/manifests;
4. общий native/Python surface;
5. conformance suite;
6. второй hardware port.

Расширение Python API, IDE или набора apps до этих контрактов будет увеличивать
стоимость будущего переноса.
