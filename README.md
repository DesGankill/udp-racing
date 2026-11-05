# UDP Racing

Учебный проект по разработке UDP-протокола и проектированию сетевой архитектуры.

Проект представляет собой сетевую основу для 2D игры-гонки с видом сверху. Взаимодействие между клиентом и выделенным сервером осуществляется по протоколу UDP.

## Архитектура

Используется топология **Dedicated Server**.

```text
Client
   │
   │ UDP
   ▼
Dedicated Server
```

Сервер отвечает за обработку игровых команд и является центральной точкой обработки сетевых сообщений.

### Основные компоненты

* `src/client.cpp` — UDP-клиент, измерения PING/PONG и серии надёжной доставки;
* `src/server.cpp` — UDP-сервер;
* `src/packet_handler.h` — разбор пакетов и ответы сервера;
* `src/protocol.h` — формат протокола и сериализация;
* `src/reliability/` — канал повторов, адаптивный RTO и окно дедупликации;
* `src/telemetry.*` — телеметрия практической работы №2.

## Поддерживаемые команды

Протокол поддерживает четыре типа пакетов:

| Значение | Тип      | Назначение                    |
| -------: | -------- | ----------------------------- |
|      `1` | MOVEMENT | Передача координат автомобиля |
|      `2` | SHOOT    | Команда использования оружия  |
|      `3` | STATE    | Передача состояния игрока     |
|      `4` | ACK      | Подтверждение команды         |

### MOVEMENT

Передаёт координаты:

* `x`
* `y`
* `z`

`requiresAck = 0`. Размер полного UDP-пакета — **20 байт**.

### SHOOT

Передаёт идентификатор оружия `weaponId`.

`requiresAck = 1`. Потерянный SHOOT повторяется. Размер полного UDP-пакета — **9 байт**.

### STATE

Содержит:

* `playerId`;
* `x`;
* `y`;
* `z`.

Размер полного UDP-пакета — **22 байта**.

### ACK

Подтверждает SHOOT. В нагрузке лежит `acknowledgedSequence` (`uint16_t`). ACK сам подтверждения не требует.

Размер полного UDP-пакета — **10 байт**.

## Сериализация

Для целочисленных полей используется сетевой порядок байтов:

```cpp
htons()
ntohs()
htonl()
ntohl()
```

Координаты `float` передаются через их битовое представление `uint32_t` с последующим преобразованием порядка байтов.

Это позволяет явно определить формат данных, передаваемых по сети.

## Требования

Для сборки проекта необходимы:

* Windows;
* MSYS2 UCRT64;
* GCC с поддержкой C++17;
* библиотека Winsock2.

## Сборка

Нужен GCC из MSYS2 UCRT64. Каталог `C:\msys64\ucrt64\bin` должен быть в `PATH`: там лежат не только `g++`, но и библиотеки времени выполнения.

Из корня репозитория:

```bat
build_all.bat
```

Скрипт собирает клиент, сервер и тесты в каталог `build/` с флагами `-std=c++17 -Wall -Wextra`.

## Запуск

Сначала сервер:

```bat
build\server.exe
```

После запуска должно появиться:

```text
UDP server started on port 54000
Waiting for packets...
```

Демонстрация практической работы №1, в которой SHOOT уже идёт через надёжный канал:

```bat
build\client.exe demo
```

Клиент отправляет 10 пакетов с интервалом в одну секунду.

Измерения практической работы №2:

```bat
build\server.exe baseline
build\client.exe baseline
```

Допустимые профили №2: `baseline`, `delay_50`, `delay_100`, `jitter`, `loss_5`, `combined`. Скрипт `run_experiments.bat` запускает все шесть. Потери этого режима действуют на PING, а не на SHOOT.

## Практическая работа №4

Тот же протокол и тот же репозиторий. Unreal не используется: роли из задания закрыты своими типами, сервер по-прежнему авторитетный. Подробности — в `docs/Prediction_Implementation.md`.

Listen-сервер здесь — процесс `prediction_server.exe`: он и считает мир, и ведёт свою машину. Второй игрок — отдельный `prediction_client.exe`. Порт `54010`, чтобы не занимать `54000` у сервера практических №1–№3.

Пакет `INPUT` несёт такт, газ, руль и просьбу рывка. Координат в нём нет. Старый `MOVEMENT` с координатами оставлен для прошлых работ и этим режимом не используется.

```bat
build\prediction_server.exe
build\prediction_client.exe
```

Клавиши клиента: `W`/`S` газ, `A`/`D` руль, `F` рывок, `C` локальный сдвиг, `P` предсказание, `X` стоп, `Q` выход. На сервере те же `W`/`A`/`S`/`D`, в `--headless` машина хоста едет сама.

Проверочный прогон:

```bat
build\prediction_server.exe --headless
build\prediction_client.exe --script --no-predict --lag-ms 100 --loss 0 --ticks 36
build\prediction_client.exe --script --lag-ms 100 --loss 0 --ticks 24
build\prediction_client.exe --script --cheat --lag-ms 180 --loss 4 --ticks 48
```

## Тесты

```bat
set PATH=C:\msys64\ucrt64\bin;%PATH%
build\test_protocol.exe
build\test_telemetry.exe
build\test_reliable_channel.exe
build\test_adaptive_timeout.exe
build\test_dedup.exe
build\test_udp_reliability.exe
```

Тесты канала и RTO используют управляемое время и не ждут реальных секунд. `test_udp_reliability.exe` гоняет настоящие UDP-сокеты на порту 54021: доставка без потерь, повтор при потере SHOOT, повтор при потере ACK без второго выстрела и отказ после пяти попыток.

## Эксперимент надёжной доставки

Сервер и клиент одной серии должны получить один и тот же профиль. Префикс `reliable` включает потери и задержку для SHOOT и ACK.

```bat
build\server.exe reliable loss_5
build\client.exe reliable loss_5
```

Все шесть серий и запись `docs/reliability_samples.csv`:

```bat
run_reliability_experiments.bat
```

Графики из этого CSV:

```bat
python make_reliability_graphs.py
```

Отчёт: `docs/Reliability_Protocol.md`.

В режиме `demo` команды чередуются:

```text
MOVEMENT
SHOOT
MOVEMENT
SHOOT
...
```

## Пример результата

Пример ответа на MOVEMENT:

```text
Sending MOVEMENT, sequence=1
Sent 20 bytes
State: playerId=1, x=11, y=20, z=0
```

Пример ответа на SHOOT:

```text
Sending SHOOT, sequence=2, requiresAck=1
Sent 9 bytes
ACK received, acknowledgedSequence=2, attempts=1
```

## Структура проекта

```text
udp-racing/
├── README.md
├── build_all.bat
├── run_reliability_experiments.bat
├── make_reliability_graphs.py
├── docs/
│   ├── Protocol_Specification.md
│   ├── Reliability_Protocol.md
│   ├── reliability_samples.csv
│   └── graphs/
└── src/
    ├── client.cpp
    ├── server.cpp
    ├── protocol.h
    └── reliability/
```

## Ограничения текущей версии

SHOOT подтверждается и при необходимости повторяется. MOVEMENT остаётся без повтора. Окно дедупликации не переживает перезапуск сервера и не даёт гарантию exactly-once вне своей ёмкости.

По-прежнему нет:

* авторизации;
* шифрования;
* CRC32;
* нескольких игровых сессий;
* полноценной синхронизации нескольких игроков.

## Результат

Проект демонстрирует:

* создание UDP-сокета;
* двусторонний обмен данными;
* структуру собственного сетевого протокола;
* сериализацию и десериализацию данных;
* использование порядкового номера пакета;
* обработку игровых команд;
* передачу состояния от сервера клиенту;
* разделение ответственности между компонентами системы.
