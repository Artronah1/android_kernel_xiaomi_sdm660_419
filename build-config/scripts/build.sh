#!/bin/bash
# build.sh — сборка кастомного ядра для Redmi Note 6 Pro (tulip)
# Kernel: android_kernel_xiaomi_sdm660_419 @ android-4.19-stable-UNI
# Features: KernelSU-Next 12759 + SUSFS 1.5.7
#
# Требования:
#   - Clang 12 (aicp-clang/clang-r416183b) в PATH или через TOOLCHAIN=
#   - GNU coreutils, zip, make, bc, bison, flex, libssl-dev
#
# Использование:
#   ./build.sh                     # собрать
#   ./build.sh clean               # очистить out/
#   ./build.sh menuconfig          # править конфиг

set -e

# --- Пути ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_CONFIG_DIR="$(dirname "$SCRIPT_DIR")"
KERNEL_DIR="$(dirname "$BUILD_CONFIG_DIR")"

OUT_DIR="$KERNEL_DIR/out"
AK3_DIR="$BUILD_CONFIG_DIR/anykernel3"
ZIP_NAME="DuckG-KSU-SUSFS-tulip-$(date +%Y%m%d-%H%M).zip"
ZIP_PATH="$KERNEL_DIR/$ZIP_NAME"

# --- Toolchain ---
if [ -n "$TOOLCHAIN" ]; then
    export PATH="$TOOLCHAIN/bin:$PATH"
fi

if ! command -v clang >/dev/null 2>&1; then
    echo "ERROR: clang не найден. Установите TOOLCHAIN=/path/to/clang или добавьте в PATH."
    exit 1
fi

echo "=== Toolchain ==="
clang --version | head -2
which clang ld.lld
echo ""

# --- Действия ---
case "${1:-build}" in
    clean)
        echo "=== Очистка ==="
        rm -rf "$OUT_DIR"
        echo "OK"
        exit 0
        ;;
    menuconfig)
        make -C "$KERNEL_DIR" O="$OUT_DIR" ARCH=arm64 \
            CC=clang LD=ld.lld AR=llvm-ar NM=llvm-nm \
            OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump STRIP=llvm-strip \
            CLANG_TRIPLE=aarch64-linux-gnu- CROSS_COMPILE=aarch64-linux-gnu- \
            CROSS_COMPILE_ARM32=arm-linux-gnueabi- menuconfig
        exit 0
        ;;
esac

# --- 1. Сборка .config ---
echo "=== 1. Сборка .config ==="
mkdir -p "$OUT_DIR"
cat "$BUILD_CONFIG_DIR/config/tulip-perf_defconfig" \
    "$BUILD_CONFIG_DIR/config/ksu.config" \
    "$BUILD_CONFIG_DIR/config/perf-tweaks.config" > "$OUT_DIR/.config"

echo "=== 2. olddefconfig ==="
yes "" | make -C "$KERNEL_DIR" O="$OUT_DIR" ARCH=arm64 \
    CC=clang LD=ld.lld AR=llvm-ar NM=llvm-nm \
    OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump STRIP=llvm-strip \
    CLANG_TRIPLE=aarch64-linux-gnu- CROSS_COMPILE=aarch64-linux-gnu- \
    CROSS_COMPILE_ARM32=arm-linux-gnueabi- olddefconfig 2>&1 | tail -3

echo ""
echo "=== Проверка KSU/SUSFS в .config ==="
grep -E "CONFIG_(KSU|KSU_SUSFS|LITTLE_CPU_MASK|BIG_CPU_MASK|SECURITY_SMACK|CORESIGHT\b)" "$OUT_DIR/.config" | head -30
echo ""

# --- 3. Сборка ---
echo "=== 3. Сборка ядра ==="
rm -f "$OUT_DIR/arch/arm64/boot/Image.gz-dtb"
make -C "$KERNEL_DIR" -j"$(nproc)" O="$OUT_DIR" ARCH=arm64 \
    CC=clang LD=ld.lld AR=llvm-ar NM=llvm-nm \
    OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump STRIP=llvm-strip \
    CLANG_TRIPLE=aarch64-linux-gnu- CROSS_COMPILE=aarch64-linux-gnu- \
    CROSS_COMPILE_ARM32=arm-linux-gnueabi- \
    2>&1 | stdbuf -oL -eL tee "$KERNEL_DIR/build.log"

# --- 4. Проверка ---
if [ ! -f "$OUT_DIR/arch/arm64/boot/Image.gz-dtb" ]; then
    echo ""
    echo "=== ОШИБКА СБОРКИ ==="
    grep -nE "error:|undefined reference|fatal:" "$KERNEL_DIR/build.log" | head -20
    exit 1
fi

echo ""
echo "=== Ядро собрано ==="
ls -la "$OUT_DIR/arch/arm64/boot/Image.gz-dtb"
grep -iE "KernelSU version|SUSFS_VERSION" "$KERNEL_DIR/build.log" | head

# --- 5. AnyKernel3 + ZIP ---
echo ""
echo "=== 5. Упаковка ZIP ==="
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
