# HackyLens Platform Roadmap

## Текущий порядок работ

Активный execution plan —
[SIMPLIFICATION_MASTERPLAN.md](SIMPLIFICATION_MASTERPLAN.md). Только он задаёт
порядок S1–S9, инварианты и exit gates текущего упрощения. Этот roadmap не
создаёт параллельные gates и не меняет статусы пакетов.

Прежняя цепочка Phase 0–10 и её platform-first/governance требования заменены
этим roadmap; исходный текст доступен в Git history. Phase 3.1–3.8 остаются
исторически завершёнными, Phase 3.9–3.17 приостановлены, Phase 4+ не начинается.
Подробные старые [Phase 2](PHASE2_MASTERPLAN.md) и
[Phase 3](PHASE3_MASTERPLAN.md) masterplans сохраняются как исторические записи.

## Ближайшая работа

- S7: завершить аппаратную приёмку и CI единого lifecycle всех 12 apps;
  миграция и удаление legacy adapter реализованы.
- S8: упрощать Capability broker за app-facing boundary по одному service,
  сохраняя реальные конфликты ресурсов, отмену, lifetime и безопасный cleanup.
- S9: удалить оставшиеся временные surfaces и устаревшую документацию,
  подтвердить итоговую firmware и завершить консолидацию текущей архитектуры.

Точные статусы и порядок миграции находятся только в masterplan. Физические
проверки повторяются для затронутых paths; новый документационный статус не
заменяет build, CI или hardware evidence.

## Сохраняемые свойства

- Рабочая firmware SEN0305/K210 и build-time app composition.
- Board-independent feature logic и один production hardware path для native
  apps и MicroPython.
- Ограниченный runtime без новых общих heap/task/queue/core/framebuffer
  механизмов ради будущих возможностей.
- Совместимость MicroPython API v1/HMPY, stable app/autostart IDs и persistence.
- Защита незавершённых asynchronous operations, один teardown deadline и
  cleanup ресурсов даже после возвращённой ошибки app stop.
- Воспроизводимая сборка и измеримые flash/static RAM/latency.

## Направления после упрощения

Это кандидаты на следующие product/research increments, а не уже начатая фаза
и не дополнительный список условий завершения S9:

- Улучшать SEN0305 features по конкретным пользовательским сценариям.
- Физически квалифицировать вторую K210-плату и проверить неизменённые apps;
  записать, какие изменения потребовались в BSP, services и приложениях.
- Расширять MicroPython API по необходимости, используя существующие typed
  services, и проверить парные Python/native приложения общими fixtures.
- Сравнить варианты архитектуры на одинаковых workloads: ресурсы, задержки,
  стоимость переноса и start/stop/cleanup behavior.

Порядок этих increments выбирается после упрощения по реальному сценарию и
доступному hardware. Они не требуют предварительно завершить Project Format,
генераторы или IDE ecosystem. Сборка Cube conformance harness не считается
физическим портом, а наличие Python и C API не считается выполненным переносом.

## Отложено до реального use case

- Project Format, package management и on-device Program Manager.
- Python-to-native skeleton generator и новые общие host frameworks.
- Dynamic loading и runtime discovery/registration.
- Расширение IDE до multi-project environment как условие firmware development.
- Стандартизация, отдельный conformance ecosystem и новые governance schemas.

Рабочий IDE/HMPY workflow сохраняется; отложено его расширение в обязательную
платформенную подсистему. Полная parity с original firmware не является условием
проверки архитектурной гипотезы.

## Где смотреть факты

- [Architecture](ARCHITECTURE.md): реализованные слои и аппаратные пути.
- [Current state](CURRENT_STATE.md): состояние реализации и ограничения.
- [Architecture vision](ARCHITECTURE_VISION.md): цели и принципы проектирования.
- [Technical contracts](spec/README.md): текущие API и правила их изменения.
- [SEN0305 physical status](PHASE2_PHYSICAL_STATUS.md): принятые observations и
  границы их применимости.

Текущий hardware acceptance не распространяется автоматически на другой image,
изменённый path или другую плату. Исторические measurements сохраняют свою
идентичность и ограничения при дальнейшем упрощении.
