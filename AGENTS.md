# AGENTS.md — ядро tulip (Redmi Note 6 Pro)

Инструкции для OpenCode при работе в этом репозитории. Подробный контекст —
в `контекст-v14.md` (актуальный) и `контекст-v8.md` (история).

## Что это

Кастомное ядро **4.19.325-st4-perf+DuckG** для Redmi Note 6 Pro (**tulip**, SDM636,
3 ГБ RAM) на LineageOS 22.2 (Android 15). CAF-база, сборка Clang 23 + ThinLTO.
Встроены KernelSU-Next 12759, SUSFS 1.5.11, Baseband Guard, BBR/FQ, MGLRU, ZRAM LZ4/ZSTD.

- Текущая ветка: `v2.5.9-clover-mm`; последний релизный тег: `v3.4.9-regulator-workqueue` (HEAD `e88418e` на 1 коммит впереди него).
  ⚠️ Имена `v3.4.8-regulator-alias` / `v3.4.9-regulator-workqueue` не соответствуют содержимому (в них f2fs sysfs fix и workqueue kfree, регуляторных коммитов нет) — подробности в `контекст-v14.md` §10.
- Апстримные теги `v3.4`–`v3.4.113` (Linux 3.4.x, 2012) — не трогать; `git push --tags` запрещён.
- Форк/пуш: `origin` = `git@github.com:Artronah1/android_kernel_xiaomi_sdm660_419.git`
- Референс: `~/Projects/kernels/ref-sdm660-419` (user-why-red/back), remote `san`/`user-why-red`.
- В репозитории **много** remote'ов (ack, clo, clover, cip, lts, los, san, ...). Репозиторий ~15 ГБ.
- Часть файловых операций идёт через симлинки: `~/Projects/kernels -> /mnt/data/kirill/Projects/kernels`,
  `~/boot -> /mnt/data/kirill/boot`.

## Сборка

Тулчейны: `~/Projects/kernels/toolchains/aosp-clang/` (внутри `clang-r614150` и др.).

```bash
cd ~/Projects/kernels/kernel-clang20-test
rm -rf out/
CLANG_VERSION=clang-r614150 ./build-config/scripts/build.sh 2>&1 \
  | tee build-$(date +%Y%m%d-%H%M).log | tail -40
LAST=$(ls -t build-*.log | head -1)
head -5 $LAST                    # должно быть clang-r614150, 23.0.1
grep -c "make.*Error" $LAST      # 0  (НЕ "error:" — ложные срабатывания A_ERROR, -Werror)
ls -la out/arch/arm64/boot/Image.gz-dtb   # ~25 МБ
```

⚠️ **Главная ловушка:** `build.sh` по умолчанию берёт Clang 22 (`clang-r584948b`) и
иногда молча использует его даже при заданном `CLANG_VERSION`. **Всегда проверяй
первые строки лога** — там должен быть `clang-r614150` / `23.0.1`. Если Clang 22 —
пересобери, явно указав `CLANG_VERSION=clang-r614150`.

Скрипт сам пишет лог в `build.log` и упаковывает AnyKernel3 ZIP
`DuckG-KSU-SUSFS-tulip-<дата>-<время>.zip` в корне дерева.

## Конфиг

Конфиг склеивается через `cat` (порядок, **последняя строка побеждает**):

```
build-config/config/tulip-perf_defconfig
build-config/config/ksu.config
build-config/config/perf-tweaks.config
```

`ksu.config`/`perf-tweaks.config` могут переопределять defconfig. Новая ветка от
старого тега **не наследует** правки defconfig — после сборки проверяй `out/.config`.

Критичные опции:

```
# CONFIG_ANDROID_SIMPLE_LMK is not set
CONFIG_PSI=y / CONFIG_PSI_DEFAULT_DISABLED=n
CONFIG_LRU_GEN=y / CONFIG_LRU_GEN_ENABLED=y
CONFIG_LTO_CLANG=y / CONFIG_LTO_CLANG_THIN=y
CONFIG_BUG=y
CONFIG_KSU_SUSFS=y (SUS_PATH, SUS_MOUNT, SUS_KSTAT, TRY_UMOUNT, SPOOF_UNAME,
                    OPEN_REDIRECT, ENABLE_LOG, HIDE_KSU_SUSFS_SYMBOLS,
                    SPOOF_CMDLINE_OR_BOOTCONFIG, AUTO_ADD_*)
CONFIG_NET_SCHED / NET_SCH_INGRESS / NET_EGRESS / NET_CLS_ACT /
CONFIG_NET_CLS_MATCHALL / NET_CLS_U32 / NET_CLS_FW / NET_ACT_SKBEDIT=y
```

Проверка одной строкой:

```bash
grep -n "SIMPLE_LMK\|PSI" build-config/config/*.config
grep -iE "SIMPLE_LMK|CONFIG_PSI|LRU_GEN|MEMCG|LTO_CLANG" out/.config
```

## Прошивка (recovery sideload)

```bash
# 1. Бэкап текущего boot (обязательно)
adb shell "su -c 'dd if=/dev/block/by-name/boot of=/sdcard/boot.img bs=4096'"
adb pull /sdcard/boot.img ~/boot/boot-backup-$(date +%Y%m%d-%H%M%S).img

# 2. Recovery + sideload
adb reboot recovery
# в recovery: "Apply update from ADB"
adb sideload /path/to/DuckG-KSU-SUSFS-tulip-*.zip
```

## Проверка после прошивки

```bash
adb shell "su -c 'dmesg'" > /tmp/boot-dmesg.log
adb logcat -b all -d > /tmp/logcat.log
# ищи: BUG / KASAN / UAF / Oops / panic; ошибки f2fs/fsck; invalid_blkaddr; vold failed
```

Норма: `/data` цел, `invalid_blkaddr=1`, `fsck.f2fs` — константа, `vold failed=0`,
BUG/KASAN/UAF=0, 5 циклов `sync; reboot` без потерь. DeepSeek не выгружается при
браузер+Telegram+YouTube (`max_cached_processes=12`; при 64 выгружался).

## Грабли (проверено на практике)

- `git push --tags` — **нельзя**: запушит тысячи upstream-тегов. Пушить тег явно по имени.
- Массовый cherry-pick (77/567/849) не работает: «clean ≠ компилируется». Патчи брать
  по подсистемам, по 10–20 штук, с проверкой сборкой после каждой партии.
- На файлах с пробелами `cut -f1` и `awk -F'\t'` ломаются — использовать `awk '{print $1}'`
  и дефолтный разделитель.
- Отложено: KPatch-Next-Module (kptools segfault).
- Молчаливый откат на Clang 22 в `build.sh` (см. «Сборка»).

## Правила работы

- Отвечай по-русски.
- Перед деструктивными действиями (`rm -rf`, прошивка, `adb reboot`, `dd` на блочные
  устройства, `git push --force`) — сначала объясни и дождись подтверждения.
- Не коммить секреты. API-ключ OpenCode лежит в `~/.config/opencode/routerai.key`.
- Изменения в ядре — атомарными коммитами с осмысленным сообщением (стиль репозитория:
  `backport(<подсистема>): <хеш> <суть>`, `mm:`, `f2fs:` и т.п.).
