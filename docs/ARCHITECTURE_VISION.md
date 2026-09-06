---
contract-id: hackylens.architecture-vision
owner: platform-architecture
version: 0.1.0
stability: experimental
---

# Architecture Vision

## Статус документа

Этот документ описывает направление проекта, а не обязательную последовательность
создания платформы. На время упрощения scope, порядок работ и exit gates задаёт
[SIMPLIFICATION_MASTERPLAN.md](SIMPLIFICATION_MASTERPLAN.md). Он имеет приоритет
при конфликте со старыми architecture/governance требованиями. Сохранённая
metadata этого документа не вводит отдельный API или compatibility promise.

Реализованное устройство описано в [ARCHITECTURE.md](ARCHITECTURE.md), состояние
и ограничения evidence — в [CURRENT_STATE.md](CURRENT_STATE.md). Цели ниже не
означают, что переносимость или Python-to-native workflow уже доказаны.

> HackyLens v0.4 is a layered K210 reference firmware and MicroPython technology
> preview.

## Цель

HackyLens развивает лёгкую переносимую архитектуру приложений для robotics
hardware. SEN0305 — первая физически проверенная реализация. Основные цели:

- подключать другие K210-платы через BSP без форка логики приложений;
- собирать firmware из изолированных feature apps;
- использовать одни hardware services из native apps и MicroPython;
- переносить Python-прототип в native app без повторной аппаратной интеграции;
- сохранять понятный lifecycle, ограниченную память и измеряемые накладные расходы.

Полезность этих свойств подтверждается приложениями, измерениями и физическими
портами. Количество specs, generators или conformance frameworks не является
результатом проекта. Создание архитектурного стандарта сейчас не является
условием разработки или подготовки научной статьи.

## Архитектурные границы

```text
Native feature apps       MicroPython scripts
        |                       |
  App lifecycle           Python bindings
        +-----------+-----------+
                    |
           Typed shared services
                    |
             Drivers / K210 HAL
                    |
             Selected board BSP
```

Схема показывает направление зависимостей, а не обещает одинаковый набор API
для обоих языков. Camera/KPU/vision bindings пока вне MicroPython API v1.

- BSP владеет wiring, board defaults, flash layout и programming metadata.
- K210 HAL предоставляет узкие platform operations без product policy.
- Drivers реализуют устройства и wire protocols, не знают feature apps.
- Services владеют общими hardware operations и необходимыми resource sessions.
- Runtime выполняет lifecycle, dispatch, переключение и cleanup одного foreground
  app. Минимальный v2 lifecycle — `start/event/render/stop`; legacy adapter
  сохраняется только до завершения миграции.
- Apps содержат feature logic и UI, не получают board pins, HAL, private
  driver/provider headers или raw SD access.
- Python bindings вызывают те же production services, сохраняя необходимые
  правила lifetime, форматов, ошибок, deadline и отмены.

Сегодня часть доступа реализована через Capability API и broker. Переход к
прямым typed services выполняется по S8; эта схема не объявляет его завершённым.

## Как добавлять и упрощать функциональность

Начальная точка — конкретный рабочий сценарий. Feature-specific logic остаётся
в app. Общая аппаратная операция использует существующий typed service;
новая общая abstraction допустима только по критерию текущего плана упрощения:
без неё ломается реальный use case, и она заменяет больше surface area, чем
добавляет. Generic Capability wrapper, runtime discovery, version negotiation,
owner/grant tables и отдельная spec не являются обязательными для каждого service.

Владение сохраняется там, где оно нужно: display transactions, light channels,
external-link mode, camera/frame borrow и core1 jobs. Поколения асинхронных
операций и fault/quarantine нельзя удалять, пока остаётся защищаемое состояние.
Один исходный teardown deadline распространяется на app stop и последующий
service cleanup; ошибка stop не пропускает cleanup.

Изменение проверяется по затронутому поведению, ресурсам и hardware paths.
Существующие public API и wire behavior не меняются только из-за правки этого
документа. Отдельный ADR, evidence schema или новый checker для обычного
рефакторинга не требуется.

## Создание приложений и Python -> native

Native app задаётся исходниками и небольшим build-time manifest. Прошивка
получает immutable registry; runtime TOML parser и dynamic loading не нужны.
App state и domain logic должны быть понятны без чтения внутренних broker tables.

Целевой Python-to-native workflow:

1. Прототип использует доступные публичные hardware services через Python API.
2. Domain state и обработка событий отделены от аппаратной реализации.
3. Native app использует те же services; перенос логики может быть ручным.
4. Общие fixtures проверяют поведение обеих реализаций.
5. Измерения фиксируют изменения кода, скорость, память и ограничения переноса.

Генератор skeleton, Project Format, Program Manager и полная трансляция Python
не являются предпосылками такого эксперимента. Их разработка отложена; команды
`new_project.py`, `run_project.py` и `new_app.py` не являются текущим workflow.

## Границы заявлений

SEN0305 runtime acceptance не доказывает работу на другой плате. Maix Cube пока
compile-conformance target; физический запуск неизменённых приложений на второй
плате остаётся отдельным результатом. Две K210-платы подтверждают переносимость
внутри проверенной K210-конфигурации, а не между произвольными MCU.

Ограниченные буферы и cooperative deadlines не означают memory isolation
недоверенного native-кода или принудительное прерывание произвольного C callback.
Гарантии формулируются по реально проверенному поведению и условиям выполнения.
