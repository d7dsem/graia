# GRAIA

Система призначена для високошвидкісного запису на диск UDP потоку/потоків данних від пристрою USRP X310 налаштованого на
захоплення 4х RF смуг по 64 MГц кожна. Що дозволяє у варіанті без перекриття захопити сумгу 256 МГц,
для подальшої офлайн обробки.

NOTE: Кожна смуга (макроканал) захоплюється незалежно. Синхронізація в часі між ними досліджується.

## Operational Philosophy: Fail-Loud

GRAIA не використовує fallback mechanisms — кожен компонент validate умови і abort при невідповідності з explicit інструкцією для оператора. 
Benchmark failed через недостатній traffic → operator must provide test stream.
Hardware changed → re-run benchmark and rebuild.

## Architecture: Capture Variants

**CURRENT (Development):** Userspace recv() baseline
- Використовується для відпрацювання file rotation, CLI, testing infrastructure
- Обмеження: копіює дані через userspace (нижчий throughput)

**ALTERNATIVE (Evaluation):** AF_PACKET + PACKET_MMAP
- Zero-copy capture через ring buffer в kernel space
- Userspace читає packets через mmap() без копіювання
- Trade-off: runtime visibility пакетів vs splice максимальний throughput
- Може бути обраний як production variant залежно від requirements balance

**TARGET (Production):** Splice-Based Zero-Copy [описано нижче]

Якщо тести на реальному залізі покуазують прийнятність більш простого варіанту зупиняємось на ньому.


## Architecture: Splice-Based Zero-Copy

Capture engine використовує Linux `splice()` syscall для zero-copy transfer: socket → pipe → file повністю в kernel space без userspace memcpy.
Це дає максимальну пропускну здатність (~95% wire speed) при мінімальному CPU overhead (2-3%).
Trade-off: headers (Ethernet 14B + IP 20B + UDP 8B = 42B) залишаються у raw files — вирізаються post-facto утилітою `graia_pp_captured`.
Альтернатива userspace runtime processing зменшує throughput на ~10% але дає runtime контроль.


## File Rotation

Capture процес пише у файли фіксованого розміру (визначається benchmark, типово 512MB-1GB). При досягненні threshold:
1. Поточний файл закривається (atomic rename з `.tmp` suffix)
2. Новий файл створюється (`graia_stream_NNNN.raw`)
3. Splice продовжується без переривання потоку

Rotation size оптимізується під конкретне залізо (SSD block size, filesystem parameters) через `graia_benchmark`.

## Termination Modes

**Normal termination:**
- `--limit N` parameter досягнуто (N GB записано) → graceful stop
- No traffic detected протягом timeout → graceful stop з log entry

**Emergency termination:**
- `kill -9` (SIGKILL) → негайний abort
- Втрата останніх 64KB-4MB даних (pipe + page cache buffers)
- Для багатогодинних sessions це статистичний шум (<0.01%)
- Post-capture gap analysis виявить missing packets

## Low-level (L1/L2/L3)

                                        10Gb
┌───────┬───────────────────────────┐         ┌─────────────────────────┬────────────────────────┐
│       │                           │   ╔═════│ 192.168.30.3:<port0>    │                        │
│       │ SPF0:  192.168.30.2:49193 │═══╣     │                         │        LINUX           │
│       │                           │   ╚═════│ 192.168.30.3:<port1>    │                        │
│  X310 │                           │         │                         │  usrp_controler_rest   │
│       │                           │   ╔═════│ 192.168.30.3:<port2>    │                        │
│       │ SPF1:  192.168.40.2:49193 │═══╣     │                         │                        │
│       │                           │   ╚═════│ 192.168.30.3:<port3>    │                        │
└───────┴───────────────────────────┘         └─────────────────────────┴────────────────────────┘

Для первинного налаштуваня на host PC потрібно запустити `usrp_controler_rest` вона дохволяє налаштувати
параметри прийому - центральні частоти макроканалів і коуфіціенти підсиленя.

Порти прийому визначаються в рантаймі. Контролер їх "тримає". 
Доступ до стріму або в режимі `raw-socket`, або після `pkill usrp_controler_rest` як до звичайних сокетів.
Пристрій Х310 тримає останні налаштування якщо контролер не виколанв `graceful shutdown`.

Є спеціальна утиліта `x310_d7` (готова), яка налаштовує пристрій і зберігає у файл `_stream_ctx.json`поточну конфігурацію.
```json
{
    "channels":[
        {
            "dev": "enp3s0fp0",
            "ip": "192.168.30.3",
            "port": "<port0>",
            "freq": 428000000,
            "gain": 75
        },
        {
            "dev": "enp3s0fp0",
            "ip": "192.168.30.3",
            "port": "<port1>",
            "freq": 452000000,
            "gain": 75
        },
        {
            "dev": "enp3s1fp1",
            "ip": "192.168.40.3",
            "port": "<port2>",
            "freq": 484000000,
            "gain": 75
        },
        {
            "dev": "enp3s1fp1",
            "ip": "192.168.40.3",
            "port": "<port3>",
            "freq": 516000000,
            "gain": 75
        },
    ],
    "timestamp": "2026-01-21 13:01:59"
}
```

## Сценарій використаня


## Data flow diagram:
```
┌──────────────────┐
│ graia_hw_probe   │
└────────┬─────────┘
         ↓
  graia_hw_manifest_<date>.json
         ↓
         ├─────────────────────────────────┐
         │                                 │
         │  ┌──────────────────┐           │
         │  │ graia_benchmark  │           │
         │  └────────┬─────────┘           │
         │           ↓                     │
         │    graia_bench_<date>.json      │
         │           ↓                     │
         │           │                     │
         └───────────┼─────────────────────┘
                     ↓
         [Operator aggregation]
                     ↓
         graia_forge_config_<date>.json
                     ↓
         ┌───────────────────┐
         │ graia_build_capt  │
         └────────┬──────────┘
                  ↓
         graia_capture_<tag>
                  ↓
         ┌───────────────────┐
         │    live-capture   │
         └────────┬──────────┘
                  ↓
            path/to/captured/graia_stream_*.raw
                  ↓
         ┌───────────────────┐
         │   PostProcessing  │
         └────────┬──────────┘
                  ↓
           [/data/stripped/graia_stream_*.raw], [manifest.json]
```

```bash
# Stage 1: HW probes.
graia_hw_probe -o graia_hw_manifest_<YYYY-MM-DD>.json

# Stage 2: Benchmark.
./graia_benchmark --test-duration 60s -o graia_bench_<YYYY-MM-DD>.json --src enp3s0fp0:192.168.30.3:50000

# Stage 3: Build.
# Operator aggregates graia_hw_manifest_<YYYY-MM-DD>.json + graia_bench_<YYYY-MM-DD>.json → creates graia_forge_config_<YYYY-MM-DD>.json
graia_build_capt.sh -i graia_forge_config_<YYYY-MM-DD>.json -o graia_capture_<tag> 

# Stage 4: Live-Capture
# (frequencies from _stream_ctx.json example)
graia_capture_tst -m 0 -f 428000000 --output-dir /data/captured --file-size 1G --src enp3s0fp0:192.168.30.3:50000 --limit 10G &

graia_capture_tst -m 1 -f 452000000 --output-dir /data/captured --file-size 1G --src enp3s0fp0:192.168.30.3:46000 --limit 10G &

graia_capture_tst -m 2 -f 484000000 --output-dir /data/captured --file-size 1G --src enp3s1fp1:192.168.40.3:49000 --limit 10G &

graia_capture_tst -m 3 -f 516000000 --output-dir /data/captured --file-size 1G --src enp3s1fp1:192.168.40.3:38000 --limit 10G &

wait  # Wait for all captures to complete

# Stage 5: Post-Procesing - Gap-Analize + [optional]Strip headrs
for f in  path/to/captured/graia_stream_452000000_*.raw; do
    graia_pp_captured -i "$f" --out-dir /data/payload --rem-capt
done

# Process Health Check: Перевірка всіх instances running:
ps aux | grep graia_capture_tst | grep -v grep

```

### Утиліта `graia_hw_probe`

Виконує оцінку параметрів host PC необхідних для визначення оптимальних compile-time параметрів збірки.
Результат `graia_hw_manifest_<date>.json`.

### Утиліта `graia_benchmark` 

Виконує підбор оптимального розімру вихідного файлу для live-capture.
Результат `graia_bench_<date>.json`.


**Output (variant):**
```txt
File Size | Throughput | CPU % | Files/sec | Recommendation
----------|------------|-------|-----------|---------------
64M       | 3.75 Gbps  | 3.2%  | 7.3       | 
128M      | 3.78 Gbps  | 2.9%  | 3.7       | 
256M      | 3.81 Gbps  | 2.6%  | 1.8       |
512M      | 3.82 Gbps  | 2.5%  | 0.9       |
1G        | 3.82 Gbps  | 2.5%  | 0.47      |
2G        | 3.81 Gbps  | 2.6%  | 0.23      |
4G        | 3.79 Gbps  | 2.8%  | 0.12      |
```

Метрика оптимальності (варіант):
Максимальний throughput
Мінімальний CPU
Balance: не забагато файлів (overhead), не замало (негнучкість)

### Утиліта/скрипт `graia_build_capt.sh`

**Input:** `graia_forge_config_<date>.json`

Оператор створює `graia_forge_config_<date>.json` агрегуючи результати з:
- `graia_hw_manifest_<date>.json` (Stage 1  `graia_hw_probe`)
- `graia_bench_<date>.json` (Stage 2  `graia_benchmark` )

Build script компілює optimized binary з параметрами з forge_config.
Критичні параметри (file size, pipe size, CPU affinity) вбудовуються як compile-time constants для максимальної ефективності.

**Output:** `graia_capture_<tag>` — optimized binary

### Утиліта `graia_capture_tst` 

Виконує запис даних у файли.

Принцип неймнігу
`path/to/captured/graia_stream_{freq_hz}_{timestamp}_{sequence}.raw`

Де:
- {freq_hz}   = Macro chan freq
- {timestamp} = capture start time (YYYY-MM-DD_hh-mm-ss)
- {sequence}  = file rotation counter (0001, 0002, ...)

**Приклад:**
```bash
path/to/captured/graia_stream_452000000_2025-01-10_21-30-00_0001.raw
path/to/captured/graia_stream_452000000_2025-01-10_21-30-00_0002.raw  # after rotation
path/to/captured/graia_stream_428000000_2025-01-10_21-30-00_0001.raw  # different macro-channel
```


### Утиліта `graia_pp_captured` 

Проводить перевірку якості записаного файлу шляхом аналізу seq_num з службового заголовку X310::PacketX310 (деталі в іншому файлі).
Опціонльно видкидає eth:ip:udp заголовки, залишає тільки udp paylod, і записує у визідний каталог. Тригер дії - наявність вхдіного параметру `--out-dir path/to`.

Видаляє файл перехоплення (незалежно від ``--out-dir`) після аналізу запису. Тригер дії - наявність вхідного парметру ` [--rem-capt]`.


## Runtime Monitoring

### Обмеження splice architecture

Кожен `graia_capture_tst` instance використовує kernel-space splice без userspace visibility пакетів. Це унеможливлює real-time підрахунок packet drops безпосередньо в capture процесі.

**Trade-off:** Максимальна пропускна здатність (splice zero-copy) vs runtime drop visibility.

**Note:** Packet drop counter відсутній у runtime (детектується post-facto).

### Interface-level monitoring (approximate)**

Періодичний polling NIC statistics окремим процесом:
```bash
#!/bin/bash
# graia_nic_monitor.sh

while true; do
    ethtool -S enp3s0fp0 | grep rx_dropped >> /var/log/graia_nic_drops.log
    ethtool -S enp3s1fp1 | grep rx_dropped >> /var/log/graia_nic_drops.log
    sleep 1
done
```

**Обмеження:**
- Показує drops на рівні interface (не per-capture instance)
- Включає drops від усіх процесів на interface
- Потребує root або `CAP_NET_ADMIN`


### Gap analysis (exact, post-facto)

Аналіз sequence numbers у X310 packet headers після запису файлу.

Використовувати `graia_pp_captured` без `--out-dir` та `--rem-capt`.
Опція Для тествоих прогонів га стендах можна  з `--rem-capt` щоб не забивалось місце на диску.

```bash
# Gap analysis без видалення файлу
for f in path/to/captured/graia_stream_*.raw; do
    graia_pp_captured -i "$f"
done

# Для тестових прогонів (з видаленням)
for f in path/to/captured/graia_stream_*.raw; do
    graia_pp_captured -i "$f" --rem-capt
done
