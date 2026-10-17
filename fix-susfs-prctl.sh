#!/bin/bash
set -e
cd ~/Projects/kernels/kernel-clang20-test
FILE="drivers/kernelsu/core_hook.c"

if [ ! -f "$FILE" ]; then
    echo "❌ Файл $FILE не найден"
    exit 1
fi

if grep -q "CMD_SUSFS_SET_ANDROID_DATA_ROOT_PATH" "$FILE"; then
    echo "⚠️  CMD_SUSFS_SET_ANDROID_DATA_ROOT_PATH уже есть в $FILE"
    echo "   Пропускаю. Проверь:"
    echo "   grep -n 'CMD_SUSFS_SET_ANDROID_DATA_ROOT_PATH' $FILE"
    exit 0
fi

echo "→ Вставляю обработку SUSFS-команд в $FILE..."

python3 << 'PYEOF'
import sys
path = "drivers/kernelsu/core_hook.c"
with open(path) as f:
    lines = f.readlines()

marker = "#endif //#ifdef CONFIG_KSU_SUSFS_SUS_PATH"
insert_idx = None
for i, line in enumerate(lines):
    if marker in line:
        insert_idx = i
        break

if insert_idx is None:
    print("❌ #endif SUS_PATH не найден")
    sys.exit(1)

fix = """\t\tif (arg2 == CMD_SUSFS_SET_ANDROID_DATA_ROOT_PATH) {
\t\t\tint error = 0;
\t\t\tif (!ksu_access_ok((void __user*)arg3, SUSFS_MAX_LEN_PATHNAME)) {
\t\t\t\tpr_err("susfs: CMD_SUSFS_SET_ANDROID_DATA_ROOT_PATH -> arg3 is not accessible\\n");
\t\t\t\treturn 0;
\t\t\t}
\t\t\terror = susfs_set_i_state_on_external_dir((char __user*)arg3,
\t\t\t\t\t\t\t\t   CMD_SUSFS_SET_ANDROID_DATA_ROOT_PATH);
\t\t\tpr_info("susfs: CMD_SUSFS_SET_ANDROID_DATA_ROOT_PATH -> ret: %d\\n", error);
\t\t\tif (!error) {
\t\t\t\tif (copy_to_user(result, &reply_ok, sizeof(reply_ok))) {
\t\t\t\t\tpr_err("prctl reply error, cmd: %lu\\n", arg2);
\t\t\t\t}
\t\t\t}
\t\t\treturn 0;
\t\t}
\t\tif (arg2 == CMD_SUSFS_SET_SDCARD_ROOT_PATH) {
\t\t\tint error = 0;
\t\t\tif (!ksu_access_ok((void __user*)arg3, SUSFS_MAX_LEN_PATHNAME)) {
\t\t\t\tpr_err("susfs: CMD_SUSFS_SET_SDCARD_ROOT_PATH -> arg3 is not accessible\\n");
\t\t\t\treturn 0;
\t\t\t}
\t\t\terror = susfs_set_i_state_on_external_dir((char __user*)arg3,
\t\t\t\t\t\t\t\t   CMD_SUSFS_SET_SDCARD_ROOT_PATH);
\t\t\tpr_info("susfs: CMD_SUSFS_SET_SDCARD_ROOT_PATH -> ret: %d\\n", error);
\t\t\tif (!error) {
\t\t\t\tif (copy_to_user(result, &reply_ok, sizeof(reply_ok))) {
\t\t\t\t\tpr_err("prctl reply error, cmd: %lu\\n", arg2);
\t\t\t\t}
\t\t\t}
\t\t\treturn 0;
\t\t}
"""
lines.insert(insert_idx, fix)
with open(path, "w") as f:
    f.writelines(lines)
print(f"✅ Вставлено перед строкой {insert_idx + 1}")
PYEOF

echo ""
echo "→ Проверка:"
grep -n "CMD_SUSFS_SET_ANDROID_DATA_ROOT_PATH\|CMD_SUSFS_SET_SDCARD_ROOT_PATH" "$FILE" || true
echo ""
echo "→ Контекст:"
sed -n '535,565p' "$FILE"
echo ""
echo "✅ Готово. Дальше: сборка, прошивка, проверка."
