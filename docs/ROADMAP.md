# HackyLens Roadmap

## 1. Все приложения как полноценные feature-модули

Привести оставшиеся приложения к той же архитектуре, которая уже используется в
`apriltag`, `face_detect`, `pong` и `terminal`.

- Перенести CAMERA, QR-CAMERA, FILES, BUTTONS, SETTINGS и SLEEP в отдельные папки
  внутри `firmware/src/apps/`.
- Каждый модуль владеет своими controller, model/service, view, config и types.
- Для остального проекта приложение предоставляет только один публичный app-header.
- Общими остаются только действительно разделяемые подсистемы: camera runtime,
  FAT32, KPU/DVP HAL, settings storage, settings-menu и vision-result contract.
- Build manifest должен включать и отключать каждый feature-модуль целиком через
  `--disable-app`.
- Architecture guard запрещает legacy-пути и использование внутренних заголовков
  feature-модуля извне.

## 2. Универсальная платформа AI-моделей

Перед добавлением новых AI-приложений отделить работу с моделями от конкретных
детекторов.

- Сделать общий lifecycle модели: load, validate, run, stop и unload.
- Добавить описание модели: входной размер и формат, labels, normalization и тип
  post-processing.
- Поддержать хранение модели и метаданных на SD без встраивания model-specific
  логики в общий KPU HAL.
- Сохранить ограничение K210: одновременно загружена только одна KPU-модель.
- Создать воспроизводимый conversion lab с закреплёнными версиями legacy nncase для
  преобразования подходящих ONNX, TFLite и Caffe-моделей в KModel v3.
- Для каждой модели проверять совместимость операций, размер KPU-памяти, размер
  входа, производительность и точность уже на устройстве.

Не любую современную нейросеть получится перенести напрямую. Практическими
кандидатами являются небольшие CNN с операциями, поддерживаемыми старым KPU:
MobileNet-подобные классификаторы, компактные YOLO-детекторы и небольшие модели
эмбеддингов. Современные модели с неподдерживаемыми слоями потребуют упрощения,
замены операций или отдельного CPU post-processing.

## 3. Object Detection на 20 классов

Сделать отдельное приложение именно с детекцией объектов и рамками, а не с одной
классификацией всего кадра.

- Исследовать найденные файлы `mobilenetv1_1.0.kmodel` и `object_detect.bin`:
  определить вход, выход, список классов и post-processing.
- Если найденная модель выдаёт координаты объектов, использовать её как основу.
- Если она является только классификатором, подобрать совместимую 20-классовую
  detection-модель, предпочтительно компактный Tiny-YOLO для K210/KModel v3.
- Реализовать полный feature-модуль с camera pipeline, worker-core inference,
  bounding boxes, class labels, confidence и settings-menu.
- Публиковать найденные объекты через единый формат BLOCK и существующий UART/I²C
  transport без отдельного несовместимого протокола.
- Отдельно измерить camera latency, inference FPS и задержку рамок относительно
  последнего кадра.

## 4. Face Recognition

Развить существующий FACE DETECT в распознавание людей. Вероятная база уже есть в
оригинальной прошивке: `detect.kmodel`, `key_point.kmodel` и `feature.kmodel`.

- Сначала подтвердить назначение, входы и выходы всех трёх моделей.
- Собрать pipeline: face detection → key points → alignment → feature embedding.
- До реализации интерфейса измерить стоимость последовательного переключения трёх
  моделей, поскольку KPU держит только одну модель одновременно.
- Добавить обучение человека с несколькими образцами лица.
- Хранить имена и нормализованные embeddings во flash с версией формата и CRC.
- Сравнивать embeddings по cosine similarity либо L2 distance с настраиваемым
  порогом и результатом `UNKNOWN` ниже порога.
- Продумать UI выбора, переименования и удаления сохранённых лиц, а также очистку
  всей базы.
- Если переключение моделей окажется слишком медленным, рассмотреть упрощённый
  pipeline или модель, объединяющую часть стадий, вместо маскировки задержки UI.

## 5. MicroPython как приложение прошивки

MicroPython должен работать внутри основной прошивки, а не заменять её. Пользователь
загружает программы из IDE, затем может запускать их как с компьютера, так и с
самого HackyLens.

Исследование архитектуры, flash-layout, протокола и этапов реализации:
[MICROPYTHON_RESEARCH.md](MICROPYTHON_RESEARCH.md).

Статус реализации: функциональный объём v1 пункта завершён. Storage/runtime/bindings
v1, приложение, HMPY v1, reference CLI и отдельный HackyLens Code repository
реализованы; host fault
tests, cross-language codec tests, executable binding/HAL/LCD tests, безопасный
hardware-acceptance runner, full/feature-disabled firmware builds и браузерный
рендер проходят. Production-образ записан на физический SEN0305; JEDEC `EF6018`
(16 МиБ) и 100 максимальных HMPY frames проходят. После явно разрешённого
`FORMAT` полный upload/read/startup/run/log/STOP/reuse/reconnect workflow прошёл,
включая остановку native `sum/min` за 121 мс и финальное восстановление пустого
userfs без startup-файла. ISP stub не поддерживает криптографический readback.
Отдельными release-qualification gates остаются проверка нескольких SEN0305,
endurance/power-loss физической NOR, WDT1 fault injection, живой browser Web
Serial edit/upload/run/log/stop/reconnect, физический BACK cleanup и 1000-cycle
run/stop stress. Контракты: [HMPY_PROTOCOL.md](HMPY_PROTOCOL.md) и
[MICROPYTHON_API.md](MICROPYTHON_API.md); WDT gate:
[WDT_HARDWARE_ACCEPTANCE.md](WDT_HARDWARE_ACCEPTANCE.md).

- [x] Добавить отдельное MicroPython-приложение с запуском, остановкой, логами и
  безопасным возвратом в главное меню.
- [x] Разместить программы во внутренней flash, а не на SD.
- [x] Сделать небольшой flash filesystem или журнал с атомарной записью, CRC,
  перечислением, удалением и выбором startup script.
- [x] Реализовать HMPY v1 framed serial protocol поверх debug UART/Web Serial
  для upload, list, read, delete, run, stop и stdout без конфликта с line debug.
- [x] Первая версия bindings: buttons, display, time, LED/RGB, UART и I²C.
- [ ] Следующая версия bindings: camera, KPU models, SD/files и vision results.
- [x] Ограничить память и время выполнения, добавить interrupt/watchdog и гарантированную
  очистку ресурсов после остановки скрипта.
- [x] Для desktop/web IDE взять за основу MIT-проект
  [Pybricks Code](https://github.com/pybricks/pybricks-code), сохранить лицензию и
  attribution, а LEGO/Bluetooth backend заменить на HackyLens Web Serial backend.
- [x] IDE должна позволять редактировать, загружать, запускать и останавливать программы,
  видеть логи и управлять файлами во flash устройства.

## Порядок реализации

1. Завершить перевод приложений в feature-папки.
2. Создать универсальный AI runtime и воспроизводимый conversion workflow.
3. Реализовать Object Detection на 20 классов.
4. Реализовать Face Recognition после измерения трёхмодельного pipeline.
5. **Выполнено:** добавлены flash-хранилище, MicroPython runtime и IDE; post-v1
   camera/KPU/SD/vision bindings остаются отдельным следующим этапом.
