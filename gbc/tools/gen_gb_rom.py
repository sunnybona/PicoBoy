#!/usr/bin/env python3
"""gen_gb_rom.py — 把 roms/ 下的 .gb/.gbc 打成一份 gb_rom.h

用法：
    python tools/gen_gb_rom.py --roms roms --out build/gb_rom.h

与 NES 那边的 gen_nes_rom.py 同一套做法：ROM 源文件放目录里，configure 时
自动打包成头文件，生成物不进工程目录。

排列顺序：若 roms/roms.list 存在就按它列出的顺序（保证菜单顺序稳定），
否则按文件名排序。

每份 ROM 编成独立 const 数组并显式放进 .rodata.pb_roms 段，汇总成 pb_roms[]
描述表；菜单显示名取文件名，下划线换成空格。
"""

import argparse
import os
import re

# 与旧 gen_rom.py 完全一致的命名规则，保证从 rom.h 迁移回来的菜单文字不变
def c_ident(name):
    stem = os.path.splitext(os.path.basename(name))[0]
    s = re.sub(r"[^0-9A-Za-z_]", "_", stem)
    if s and s[0].isdigit():
        s = "_" + s
    return s.lower()


def nice_name(name):
    stem = os.path.splitext(os.path.basename(name))[0]
    return stem.replace("_", " ").strip()


def rom_info(data):
    """(MBC 名, 是否 CGB, 是否 2 的幂)"""
    mbc = "OTHER"
    if len(data) > 0x147:
        t = data[0x147]
        if t == 0x00:
            mbc = "NO MBC"
        elif 0x01 <= t <= 0x03:
            mbc = "MBC1"
        elif 0x05 <= t <= 0x06:
            mbc = "MBC2"
        elif 0x0F <= t <= 0x13:
            mbc = "MBC3"
        elif 0x19 <= t <= 0x1E:
            mbc = "MBC5"
    cgb = len(data) > 0x143 and (data[0x143] & 0x80) != 0
    pow2 = len(data) > 0 and (len(data) & (len(data) - 1)) == 0
    return mbc, cgb, pow2


def load_order(rom_dir, files):
    """roms.list 存在就按它排，否则按文件名排"""
    order_file = os.path.join(rom_dir, "roms.list")
    if not os.path.isfile(order_file):
        return files

    listed = []
    with open(order_file, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            p = os.path.join(rom_dir, line)
            if os.path.isfile(p):
                listed.append(p)
            else:
                print("  ! roms.list 列出的文件不存在，跳过: %s" % line)

    # 未列出的文件补在后面，避免悄悄丢 ROM
    rest = [p for p in files if p not in listed]
    if rest:
        print("  ! 有 %d 份 ROM 不在 roms.list 里，追加到末尾" % len(rest))
    return listed + rest


def emit_bytes(out, data):
    for i in range(0, len(data), 12):
        chunk = data[i:i + 12]
        out.write("    " + ",".join("0x%02X" % b for b in chunk))
        out.write(",\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--roms", default="roms", help="ROM 目录")
    ap.add_argument("--out", required=True, help="输出的 gb_rom.h 路径")
    args = ap.parse_args()

    rom_dir = args.roms
    files = []
    if os.path.isdir(rom_dir):
        for name in sorted(os.listdir(rom_dir)):
            if name.lower().endswith((".gb", ".gbc")):
                files.append(os.path.join(rom_dir, name))
    files = load_order(rom_dir, files)

    entries = []
    seen = set()
    for path in files:
        with open(path, "rb") as f:
            data = f.read()

        ident = c_ident(path)
        if ident in seen:
            ident = ident + "_%d" % len(seen)
        seen.add(ident)

        mbc, cgb, pow2 = rom_info(data)
        if not pow2:
            print("  ! %s 不是 2 的幂大小，模拟器启动会拒绝加载" % path)

        entries.append((ident, nice_name(path), data))
        print("  + %-46s %7d B  %-7s %s"
              % (nice_name(path), len(data), mbc, "CGB" if cgb else "DMG"))

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)

    with open(args.out, "w", encoding="utf-8", newline="\n") as out:
        out.write("/* 由 gbc/tools/gen_gb_rom.py 自动生成 —— 不要手改 */\n")
        out.write("#ifndef PB_ROMS_EMBED_H\n#define PB_ROMS_EMBED_H\n")
        out.write("#include <stdint.h>\n")
        out.write('#include "pb_roms.h"   /* pb_rom_entry_t */\n\n')
        out.write("#ifndef PB_ROM_FLASH\n")
        out.write('#define PB_ROM_FLASH '
                  '__attribute__((section(".rodata.pb_roms"), used))\n')
        out.write("#endif\n\n")

        for ident, label, data in entries:
            out.write("PB_ROM_FLASH\n")
            out.write("static const uint8_t pb_rom_%s[%d] = {\n"
                      % (ident, len(data)))
            emit_bytes(out, data)
            out.write("};\n\n")

        out.write("static const pb_rom_entry_t pb_roms[%d] = {\n" % max(1, len(entries)))
        if entries:
            for ident, label, data in entries:
                out.write('    { "%s", pb_rom_%s, %d },\n'
                          % (label, ident, len(data)))
        else:
            out.write('    { "NO ROM", 0, 0 },\n')
        out.write("};\n\n")
        out.write("#define PB_ROM_COUNT "
                  "(sizeof(pb_roms) / sizeof(pb_roms[0]))\n\n")
        out.write("#endif /* PB_ROMS_EMBED_H */\n")

    total = sum(len(e[2]) for e in entries)
    print("=> %s ：%d 份 ROM，共 %d 字节（%.1f KiB）"
          % (args.out, len(entries), total, total / 1024.0))


if __name__ == "__main__":
    main()
