#!/usr/bin/env python3
import re

PATH = "mm/madvise.c"

def fix(path):
    with open(path, "r") as f:
        src = f.read()

    src = re.sub(
        r'\nstatic const struct mm_walk_ops cold_walk_ops = \{[^}]*\};\n',
        '\n', src, count=1)

    def replace_walk(m):
        indent = m.group(1)
        return (
            f"{indent}struct mm_walk walk = {{\n"
            f"{indent}    .pmd_entry = madvise_cold_or_pageout_pte_range,\n"
            f"{indent}    .mm = vma->vm_mm,\n"
            f"{indent}    .vma = vma,\n"
            f"{indent}    .private = &walk_private,\n"
            f"{indent}}};\n"
            f"{indent}walk_page_range(addr, end, &walk);"
        )

    src = re.sub(
        r'([ \t]*)walk_page_range\(vma->vm_mm, addr, end, &cold_walk_ops, &walk_private\);',
        replace_walk, src)

    with open(path, "w") as f:
        f.write(src)

    print(f"OK: {path} исправлен")

if __name__ == "__main__":
    fix(PATH)
