#!/usr/bin/env python3
"""gen_nes_rom.py — 把 roms/ 下的 .nes 打成一份 nes_rom.h

用法：
    python tools/gen_nes_rom.py --out build/nes_rom.h [--roms roms]

ROM 会被编进固件、放在 flash 里（PicoBoy 是 16MB flash），不进 RAM。
每份 ROM 保留完整 .nes 文件（含 16 字节 iNES 头），INFO NES 运行时
直接把 ROM 指针指到数组里，省一次拷贝。
"""

import argparse
import os
import re
import struct

C_ESCAPE = {}


def human_name(path):
    stem = os.path.splitext(os.path.basename(path))[0]
    stem = re.sub(r"[^0-9A-Za-z .:_+/!?-]", " ", stem)
    stem = re.sub(r"\s+", " ", stem).strip().upper()
    return stem[:29] if stem else "UNNAMED"


def parse_ines(data):
    """返回 (mapper, prg_kb, chr_kb)；不是合法 iNES 返回 None"""
    if len(data) < 16 or data[:4] != b"NES\x1a":
        return None

    flags6, flags7 = data[6], data[7]
    mapper = (flags6 >> 4) | (flags7 & 0xF0)
    prg_kb = data[4] * 16
    chr_kb = data[5] * 8

    if flags6 & 0x04:           # trainer
        body = 16 + 512
    else:
        body = 16

    if body + prg_kb * 1024 + chr_kb * 1024 > len(data):
        return None
    return mapper, prg_kb, chr_kb


def emit_bytes(out, data):
    step = 16
    for i in range(0, len(data), step):
        chunk = data[i:i + step]
        out.write("  " + ",".join("0x%02x" % b for b in chunk))
        out.write(",\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--roms", default="roms", help="ROM 目录")
    ap.add_argument("--out", required=True, help="输出的 nes_rom.h 路径")
    args = ap.parse_args()

    rom_dir = args.roms
    files = []
    if os.path.isdir(rom_dir):
        for name in sorted(os.listdir(rom_dir)):
            if name.lower().endswith(".nes"):
                files.append(os.path.join(rom_dir, name))

    entries = []
    for path in files:
        with open(path, "rb") as fp:
            data = fp.read()
        info = parse_ines(data)
        name = human_name(path)
        if info is None:
            print("  skip (坏文件/不是 iNES): %s" % path)
            continue
        entries.append((name, info, data, path))
        print("  + %-30s mapper %-3d PRG %4dK CHR %4dK  %6.1f KiB"
              % (name, info[0], info[1], info[2], len(data) / 1024.0))

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)

    with open(args.out, "w", encoding="utf-8", newline="\n") as out:
        out.write("/* 由 tools/gen_nes_rom.py 自动生成 —— 不要手改 */\n")
        out.write("#ifndef NES_ROM_H\n#define NES_ROM_H\n\n")
        out.write('#include "pb_nes.h"\n\n')

        for i, (name, info, data, path) in enumerate(entries):
            out.write("/* %s  (%s) */\n" % (name, os.path.basename(path)))
            out.write("static const uint8_t nes_rom_%d[] "
                      "__attribute__((aligned(4))) = {\n" % i)
            emit_bytes(out, data)
            out.write("};\n\n")

        out.write("const pb_nes_rom_t pb_nes_roms[] = {\n")
        if entries:
            for i, (name, info, data, path) in enumerate(entries):
                out.write('  { "%s", %d, %d, %d, nes_rom_%d, sizeof(nes_rom_%d) },\n'
                          % (name, info[0], info[1], info[2], i, i))
        else:
            out.write('  { "NO ROM", 0, 0, 0, 0, 0 },\n')
        out.write("};\n\n")
        out.write("#define PB_NES_ROM_COUNT %d\n\n" % len(entries))
        out.write("#endif /* NES_ROM_H */\n")

    total = sum(len(e[2]) for e in entries)
    print("=> %s ：%d 份 ROM，共 %.1f KiB" % (args.out, len(entries), total / 1024.0))


if __name__ == "__main__":
    main()
