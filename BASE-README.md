# san-resukisu-base — база под ядро san-kernel (tulip)

Рабочее дерево для эксперимента с **исходным** ядром `user-why-red` (san-kernel)
и портирования на него наших наработок.

## Что это

- **Upstream:** `https://github.com/user-why-red/android_kernel_xiaomi_sdm660_419`
- **Ветка:** `resukisu-v4.2.0(35115)-susfs-v2.3.0`
- **Коммит:** `eef8fbd49d113` («san-kernel: Update release tags»)
- **Версия:** `4.19.325`, `CONFIG_LOCALVERSION="-Artronah-San-Kernel-R1.1.108"`
- **ReSukiSU:** v4.2.0-rc3, сабмодуль `KernelSU` = `516d3e87` (driver 35257)
- **SuSFS:** v2.3.0 (inline hooks, `CONFIG_KSU_SUSFS=y`)
- **Deep idle:** восстановлен (`cpuidle: lpm-levels: restore deep idle states`)
- **eBPF inbound (sing-box/AsteriskBox):** включён `CONFIG_NET_SCH_INGRESS` (tc/clsact)

### Как создано

Ветка уже была зафетчена в общий `.git` большого репозитория (remote `san` /
`user-why-red`). Отдельно клонировать 15 ГБ не понадобилось — сделано worktree:

```bash
git -C ~/Projects/kernels/kernel-clang20-test \
    worktree add -b v2.8.0-san-base \
    ~/Projects/kernels/san-resukisu-base \
    "san/resukisu-v4.2.0(35115)-susfs-v2.3.0"
```

Локальная ветка: **`v2.8.0-san-base`** (tracking `san/…`).

Сабмодуль KernelSU инициализирован **локально** (сеть для него не потребовалась,
т.к. нужный коммит уже был в `ksu-port-test/KernelSU`):

```bash
git clone --no-checkout --shared <ksu-port-test>/KernelSU KernelSU
git -C KernelSU remote set-url origin https://github.com/ReSukiSU/ReSukiSU
git -C KernelSU checkout 9d0ff6aea9e25fc7dd26f4643175a41f68375e5e
```

## Тулчейн

- **Clang 23:** `~/Projects/kernels/toolchains/aosp-clang/clang-r614150`
  (`Android (16464736, based on r614150) clang version 23.0.1`).
- PATH подставляется автоматически скриптом сборки.
- Хост-кросс-компилятор `aarch64-linux-gnu-gcc` есть в системе (`/usr/bin`).
- ARM32 (`CROSS_COMPILE_ARM32=arm-linux-gnueabi-`) не обязателен при `LLVM_IAS=1`.

## Сборка

```bash
cd ~/Projects/kernels/san-resukisu-base
./build-config/scripts/build.sh                # Clang 23, родной defconfig
CLANG_VERSION=clang-r584948b ./build.sh        # другим Clang
USE_FRAGMENTS=1 ./build.sh                     # + накатить наши ksu/perf-tweaks фрагменты
./build.sh clean                               # очистить out/
./build.sh menuconfig                          # править .config
```

Результат: `out/arch/arm64/boot/Image.gz-dtb` и
`SanKernel-KSU-SUSFS-tulip-<дата>-<время>.zip` в корне дерева.

### ⚠️ Важные отличия от наших деревьев

1. **Defconfig — родной** `arch/arm64/configs/vendor/tulip-perf_defconfig`
   (в нём уже `CONFIG_KSU=y`, `CONFIG_KSU_SUSFS=y`, `LRU_GEN`, `SCHED_CASS`).
   `make` принимает цель **без префикса** `arch/arm64/configs/`: `vendor/tulip-perf_defconfig`.
2. **Наш `build-config/config/ksu.config` НЕ совместим** с этой веткой: он от
   KernelSU-Next, а здесь ReSukiSU с другим набором SUSFS-опций
   (`KSU_SUSFS_SUS_MAP` вместо `TRY_UMOUNT`/`OPEN_REDIRECT`-набора и т.д.).
   Родной defconfig уже включает нужное. Фрагменты положены «на будущее».
3. **SUSFS inline hooks уже наложены** в дереве (`susfs_inline_hook_patches.sh`,
   `fs/exec.c`, `fs/open.c`, ... уже содержат `ksu_handle_*`).
4. **Baseband Guard в дереве нет** — при портировании нужно ставить отдельно
   (патчи лежат в `build-config/patches/`).
5. HZ=250 у этой ветки (у нас HZ=300, у доноров HZ=100).

## Структура

```
build-config/
├── config/
│   ├── ksu.config          — наши опции KSU/SUSFS (от KernelSU-Next; несовместимы, см. выше)
│   └── perf-tweaks.config  — BBR/FQ, ZSTD, hardening, BBG, kprofiles
├── patches/
│   ├── 0001-mglru-promote-exec-folios.patch
│   ├── 0002-bbg-selinux-cred.patch
│   └── bbg/                — исходники Baseband-guard
├── anykernel3/             — упаковщик ZIP (anykernel.sh: tulip/twolip/bouquet)
└── scripts/
    └── build.sh            — сборка в один шаг
```

## Что уже проверено

- Тулчейн clang-r614150 работает.
- Конфиг генерируется, критические опции на месте
  (`KSU=y`, `KSU_SUSFS=y`, `LRU_GEN=y/ENABLED`, `LTO_CLANG_THIN=y`,
   `SCHED_CASS=y`, `ENERGY_MODEL=y`, `BUG=y`).
- `KSU_MANUAL_HOOK`/`KSU_TRACEPOINT_HOOK` выключены — как и в рабочем
  `ksu-port-test`; хук работает через `KSU_SUSFS` (inline).
- Первая полная сборка — см. лог `build-<дата>-<время>.log`.
