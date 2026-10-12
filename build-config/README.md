Сборочные артефакты для кастомного ядра Redmi Note 6 Pro (**tulip** / **twolip** / **bouquet**).

## Что внутри

- **Kernel:** `android_kernel_xiaomi_sdm660_419` — ветка `android-4.19-stable-UNI`
- **KernelSU-Next:** v12759
- **SUSFS:** v1.5.7
- **Hook:** `CONFIG_KSU_LSM_SECURITY_HOOKS=y` (LSM, не kprobes)
- **Менеджер KSU:** v3.4.0-legacy (совместим по подписи `79e59011...`)

## Структура

build-config/
├── README.md — этот файл
├── config/
│ ├── tulip-perf_defconfig — базовый defconfig (LOCALVERSION, CPU masks, FTS touchscreen)
│ ├── ksu.config — фрагмент: KSU + SUSFS (все 15 опций)
│ └── tulip-final.config — итоговый .config (для справки, ~160 KB)
├── anykernel3/ — AnyKernel3 zip-упаковщик (прошивка через recovery sideload)
└── scripts/
└── build.sh — сборка в один шаг
text


## Требования

- **Clang 12** — `aicp-clang/clang-r416183b` (Android 12.0.5, r416183b) ~1.5 GB
- **Утилиты:** `make`, `bc`, `bison`, `flex`, `libssl-dev`, `zip`, `rsync`
- **ОС:** Linux (x86_64), 8+ GB RAM, 30+ GB свободного места

### Где взять Clang 12

```bash
mkdir -p ~/toolchains && cd ~/toolchains
git clone --depth=1 https://github.com/AICP/prebuilts_clang_host_linux-x86_clang-r416183b.git aicp-clang
export TOOLCHAIN=~/toolchains/aicp-clang

Или любой совместимый Android Clang 12 / r416183b.
Сборка
bash

# Из корня дерева ядра (где .git + arch/ + drivers/ + ...)
export TOOLCHAIN=~/toolchains/aicp-clang
./build-config/scripts/build.sh

Результат: DuckG-KSU-SUSFS-tulip-YYYYMMDD-HHMM.zip в корне дерева.
Действия
bash

./build-config/scripts/build.sh            # собрать
./build-config/scripts/build.sh clean      # очистить out/
./build-config/scripts/build.sh menuconfig # править .config

Прошивка

Через recovery (sideload):
bash

# 1. Перезагрузить в recovery
adb reboot recovery

# 2. Когда recovery загрузится — выбрать "Apply update from ADB"
adb sideload DuckG-KSU-SUSFS-tulip-*.zip

Или через TWRP/OrangeFox Install — перекинуть ZIP на устройство и установить.
Восстановление при проблемах

Перед прошивкой сохрани текущий boot:
bash

adb shell "su -c 'dd if=/dev/block/by-name/boot of=/sdcard/boot.img bs=4096'"
adb pull /sdcard/boot.img ~/boot-backup-$(date +%Y%m%d).img
adb shell "su -c 'rm /sdcard/boot.img'"

При проблемах — fastboot flash boot boot-backup-YYYYMMDD.img.
Известные особенности

    CONFIG_SECURITY_SMACK — выключен. В дереве smack_lsm.c использует старый API (socket_smack *), несовместимый с 4.19.325 (sk_security_struct [1]). На Android используется SELinux.

    CONFIG_CORESIGHT — выключен. Аналогичная проблема с API.

    CONFIG_TOUCHSCREEN_SYNAPTICS_DSX_E7S — выключен. tulip использует Novatek/FTS, не Synaptics. mdss_io_util.c и mdss_dsi_panel.c защищены патчем a143e2c60e00.

    CONFIG_KPROBES — выключен. KSU работает через LSM security hook, стабильнее на 4.19.

Обновление от upstream
bash

git fetch upstream
git checkout android-4.19-stable-UNI
git merge upstream/android-4.19-stable-UNI   # или rebase
git push origin android-4.19-stable-UNI

Ссылки

    KernelSU-Next

    SUSFS4KSU

    AnyKernel3

    Оригинальное дерево

## MGLRU: promote executable file pages (2026-10-12)

Добавлен патч `patches/0001-mglru-promote-exec-folios.patch`, который
промоутит **mapped executable file pages** (код приложений, `.so`) после
первого использования. Это компенсирует слабое место MGLRU — исполняемый
код вытесняется слишком агрессивно по сравнению с классическим LRU.

### Эффект
- Лучше удержание кода в памяти
- Меньше I/O thrashing
- Быстрее холодный старт приложений

### Реализация
- `mm/Kconfig`: `CONFIG_LRU_GEN_EXEC_PROMOTE`
- `mm/vmscan.c`: promote в 3 местах:
  - `walk_pte_range` (PTE, `walk->vma`)
  - `walk_pmd_range_locked` (PMD, `walk->vma`)
  - `lru_gen_look_around` (`pvmw->vma`)
- `build-config/config/perf-tweaks.config`: `CONFIG_LRU_GEN_EXEC_PROMOTE=y`

### Проверка
```bash
adb shell "su -c 'cat /sys/kernel/mm/lru_gen/enabled'"
# → 0x0001

adb shell "su -c 'cat /sys/kernel/mm/lru_gen/min_ttl_ms'"
# → 0
Проверенная сборка

    Дата: 2026-10-12

    Ядро: 22 056 678 байт

    ZIP: DuckG-KSU-SUSFS-BBR-ZSTD-MGLRU-tulip-*.zip (17 461 390)

    Статус: boot OK, MGLRU 0x0001, BBR, ZRAM [zstd], KSU/SUSFS работают
    EOF

cat build-config/README.md | tail -50

## 🎯 Commit + push

```bash
cd ~/Projects/kernels/kernel-duckyduck-419

git status --short

git add build-config/README.md build-config/patches/

git commit -m "docs: add MGLRU promote section to README, save MGLRU patch

- build-config/README.md: MGLRU exec-promote documentation
- build-config/patches/0001-mglru-promote-exec-folios.patch: patch from commit 55a5ce49"

git log --oneline -5
git push origin android-4.19-stable-UNI

## Baseband Guard (BBG) — 2026-10-12

Портирован **Baseband Guard** — LSM-модуль, блокирующий несанкционированную
запись в критичные партиции (`modem`, `abl`, `ablbak`, `xbl`, и т.д.) и
device nodes на уровне ядра.

### Что защищено
- `modem`, `abl`, `ablbak`, `xbl`, `xblbak` — критичные для baseband/bootloader
- НЕ защищены: `boot`, `recovery` (можно прошивать через sideload/fastboot)

### Как работает
- `bb_bprm_set_creds` — при execve проверяет, не запущен ли процесс из доменов
  `u:r:su:s0` / `u:r:ksu:s0` / `u:r:magisk:s0`. Если да — помечает как
  `untrusted_process`.
- Хуки на `file_ioctl`, `file_open`, `file_permission` блокируют запись
  untrusted-процессов в защищённые блочные устройства.

### Особенности порта на 4.19
- В 4.19 нет `lsm_blob_sizes` → cred-данные хранятся в `task_security_struct`
  (поле `bbg_cred` в `security/selinux/include/objsec.h`)
- Регистрация через `DEFINE_LSM` без `.blobs`
- Порядок инициализации: `__lsm_selinux` < `__lsm_baseband_guard`
  (проверено через `llvm-nm -n out/vmlinux`)

### Проверка
```bash
adb shell "su -c 'dmesg | grep -i baseband'"
# → baseband_guard power by ...
# → baseband_guard: pid NNN has been marked as untrusted process ...
Файлы

    patches/0002-bbg-selinux-cred.patch — патч objsec.h

    patches/bbg/ — исходники BBG (пропатченные для 4.19)

    config/perf-tweaks.config — CONFIG_BBG=y
    EOF

wc -l build-config/README.md
tail -20 build-config/README.md

## Baseband Guard (BBG) — 2026-10-12

Портирован **Baseband Guard** — LSM-модуль, блокирующий несанкционированную
запись в критичные партиции (`modem`, `abl`, `ablbak`, `xbl`, `xblbak`) и
device nodes на уровне ядра.

### Что защищено
- `modem`, `abl`, `ablbak`, `xbl`, `xblbak` — критичные для baseband/bootloader
- НЕ защищены: `boot`, `recovery` (можно прошивать через sideload/fastboot)

### Как работает
- `bb_bprm_set_creds` — при execve проверяет, не запущен ли процесс из доменов
  `u:r:su:s0` / `u:r:ksu:s0` / `u:r:magisk:s0`. Если да — помечает как
  `untrusted_process`.
- Хуки на `file_ioctl`, `file_open`, `file_permission` блокируют запись
  untrusted-процессов в защищённые блочные устройства.

### Особенности порта на 4.19
- В 4.19 нет `lsm_blob_sizes` → cred-данные хранятся в `task_security_struct`
  (поле `bbg_cred` в `security/selinux/include/objsec.h`)
- Регистрация через `DEFINE_LSM` без `.blobs`
- Порядок инициализации: `__lsm_selinux` < `__lsm_baseband_guard`
  (проверено через `llvm-nm -n out/vmlinux`)

### Проверка на устройстве
```bash
adb shell "su -c 'dmesg | grep -i baseband'"
# → baseband_guard power by ...
# → baseband_guard: pid NNN has been marked as untrusted process ...
Файлы

    patches/0002-bbg-selinux-cred.patch — патч objsec.h

    patches/bbg/ — исходники BBG (пропатченные для 4.19)

    config/perf-tweaks.config — CONFIG_BBG=y
