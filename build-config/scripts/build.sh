#!/bin/bash
# build.sh — сборка ядра san-kernel (ReSukiSU v4.2.0 + SuSFS v2.3.0) для Redmi Note 6 Pro (tulip)
# Kernel: android_kernel_xiaomi_sdm660_419 @ resukisu-v4.2.0(35115)-susfs-v2.3.0
#
# База: ветка user-why-red «resukisu-v4.2.0(35115)-susfs-v2.3.0», коммит eef8fbd49d113.
# Клон дерева: ~/Projects/kernels/san-resukisu-base (worktree общего .git).
#
# Требования:
#   - Clang (по умолчанию clang-r614150 = Clang 23) через TOOLCHAIN= или CLANG_VERSION=
#   - GNU coreutils, zip, make, bc, bison, flex, libssl-dev
#
# Использование:
#   ./build.sh                                # собрать (Clang 23, родной defconfig)
#   CLANG_VERSION=clang-r584948b ./build.sh   # собрать другим Clang
#   USE_FRAGMENTS=1 ./build.sh                # поверх defconfig накатить ksu.config + perf-tweaks.config
#   ./build.sh clean                          # очистить out/
#   ./build.sh menuconfig                     # править .config

set -e

# --- Пути ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_CONFIG_DIR="$(dirname "$SCRIPT_DIR")"
KERNEL_DIR="$(dirname "$BUILD_CONFIG_DIR")"

OUT_DIR="$KERNEL_DIR/out"
AK3_DIR="$BUILD_CONFIG_DIR/anykernel3"
ZIP_NAME="Artronah-San-Kernel-KSU-SUSFS-tulip-$(date +%Y%m%d-%H%M).zip"
ZIP_PATH="$KERNEL_DIR/$ZIP_NAME"

# Родной defconfig этой ветки (уже содержит KSU/SUSFS/LRU_GEN/CASS).
# ВАЖНО: make принимает цель БЕЗ префикса arch/arm64/configs/ — то есть vendor/tulip-perf_defconfig.
DEFCONFIG_REL="vendor/tulip-perf_defconfig"

# --- Toolchain ---
# Дефолт: Clang 23 (AOSP r614150). Переопределение:
#   CLANG_VERSION=clang-r584948b ./build.sh
#   TOOLCHAIN=/path/to/clang ./build.sh
TOOLCHAIN_BASE="$HOME/Projects/kernels/toolchains/aosp-clang"
CLANG_VERSION="${CLANG_VERSION:-clang-r614150}"
TOOLCHAIN="${TOOLCHAIN:-$TOOLCHAIN_BASE/$CLANG_VERSION}"

# Проверка, что тулчейн существует
if [ ! -d "$TOOLCHAIN" ]; then
    echo "ERROR: Toolchain не найден: $TOOLCHAIN"
    echo "Доступные:"
    ls -1 "$TOOLCHAIN_BASE" 2>/dev/null || echo "  (нет)"
    exit 1
fi

export PATH="$TOOLCHAIN/bin:$PATH"

if ! command -v clang >/dev/null 2>&1; then
    echo "ERROR: clang не найден в $TOOLCHAIN/bin"
    exit 1
fi

echo "=== Toolchain ==="
echo "TOOLCHAIN=$TOOLCHAIN"
clang --version | head -2
which clang ld.lld
echo ""

# Общая командная строка make
MAKE_ARGS=(
    -C "$KERNEL_DIR" O="$OUT_DIR" ARCH=arm64 LLVM=1 LLVM_IAS=1
    CC=clang LD=ld.lld AR=llvm-ar NM=llvm-nm
    OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump STRIP=llvm-strip
    CLANG_TRIPLE=aarch64-linux-gnu- CROSS_COMPILE=aarch64-linux-gnu-
    CROSS_COMPILE_ARM32=arm-linux-gnueabi-
)

# --- Действия ---
case "${1:-build}" in
    clean)
        echo "=== Очистка ==="
        rm -rf "$OUT_DIR"
        echo "OK"
        exit 0
        ;;
    menuconfig)
        make "${MAKE_ARGS[@]}" menuconfig
        exit 0
        ;;
esac

# --- 1. Сборка .config ---
echo "=== 1. .config из родного defconfig ==="
mkdir -p "$OUT_DIR"
make "${MAKE_ARGS[@]}" "$DEFCONFIG_REL" 2>&1 | tail -3

if [ "${USE_FRAGMENTS:-0}" = "1" ]; then
    echo "=== 1b. Наложение фрагментов (ksu.config + perf-tweaks.config) ==="
    cat "$OUT_DIR/.config" \
        "$BUILD_CONFIG_DIR/config/ksu.config" \
        "$BUILD_CONFIG_DIR/config/perf-tweaks.config" > "$OUT_DIR/.config.frag"
    mv "$OUT_DIR/.config.frag" "$OUT_DIR/.config"
fi

echo "=== 2. olddefconfig ==="
yes "" | make "${MAKE_ARGS[@]}" olddefconfig 2>&1 | tail -3

echo ""
echo "=== Проверка критичных конфигов ==="
grep -E "CONFIG_(KSU|KSU_SUSFS|LRU_GEN|LTO_CLANG|HZ=|ANDROID_SIMPLE_LMK)" "$OUT_DIR/.config" | head -30
echo ""

echo "=== Проверка BUG / PANIC_ON_OOPS ==="
if grep -q "^CONFIG_BUG=y" "$OUT_DIR/.config"; then
    echo "  CONFIG_BUG=y"
else
    echo "  ВНИМАНИЕ: CONFIG_BUG=n (BUG() = no-op)"
fi

if grep -q "^CONFIG_PANIC_ON_OOPS=y" "$OUT_DIR/.config"; then
    echo "  ВНИМАНИЕ: CONFIG_PANIC_ON_OOPS=y (oops -> reboot)"
else
    echo "  CONFIG_PANIC_ON_OOPS=n (oops -> log)"
fi
echo ""

# --- 3. Сборка ---
echo "=== 3. Сборка ядра ==="
rm -f "$OUT_DIR/arch/arm64/boot/Image.gz-dtb"
make "${MAKE_ARGS[@]}" -j"$(nproc)" \
    2>&1 | stdbuf -oL -eL tee "$KERNEL_DIR/build-$(date +%Y%m%d-%H%M).log"

# --- 4. Проверка ---
if [ ! -f "$OUT_DIR/arch/arm64/boot/Image.gz-dtb" ]; then
    echo ""
    echo "=== ОШИБКА СБОРКИ ==="
    grep -nE "error:|undefined reference|fatal:" "$KERNEL_DIR"/build-*.log | tail -20
    exit 1
fi

echo ""
echo "=== Ядро собрано ==="
ls -la "$OUT_DIR/arch/arm64/boot/Image.gz-dtb"

# --- 5. AnyKernel3 + ZIP ---
echo ""
echo "=== 5. Упаковка ZIP ==="

cat > "$AK3_DIR/toolchain.txt" << EOF
TOOLCHAIN=$TOOLCHAIN
CLANG_VERSION=$(clang --version | head -1)
BUILD_DATE=$(date)
EOF

cp "$OUT_DIR/arch/arm64/boot/Image.gz-dtb" "$AK3_DIR/Image.gz-dtb"

cd "$AK3_DIR"
rm -f "$ZIP_PATH"
zip -r9 "$ZIP_PATH" . \
    -x ".git/*" -x ".github/*" -x "*.bak*" -x "*.zip" \
    -x "README.md" -x ".gitignore" -x "LICENSE" \
    > /dev/null

cd "$KERNEL_DIR"
echo ""
echo "=== ГОТОВО ==="
ls -la "$ZIP_PATH"
echo ""
echo "Прошивка:"
echo "  adb reboot recovery"
echo "  adb sideload $ZIP_PATH"
