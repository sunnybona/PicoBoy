#!/usr/bin/env python3
"""unpack_gb_rom.py - 从旧的 gbc/rom.h 还原出 roms/*.gb 源文件（一次性迁移工具）

旧工程只提交了生成物 rom.h（19.5 MB 的 C 数组），没有 .gb 源文件。
这个脚本把数组里的字节写回成独立 ROM 文件，并按 rom.h 里的表顺序生成
roms/roms.list，使重新打包后的菜单顺序与原来完全一致。

用法：
    python tools/unpack_gb_rom.py gbc/rom.h gbc/roms
"""

import io
import os
import re
import sys

START = re.compile(r"static const uint8_t pb_rom_(\w+)\[(\d+)\] = \{")
BYTE = re.compile(r"0x([0-9A-Fa-f]{2})")
TABLE = re.compile(r'\{\s*"([^"]+)",\s*pb_rom_(\w+),\s*(\d+)\s*\}')


def main():
    if len(sys.argv) < 3:
        print("usage: unpack_gb_rom.py <rom.h> <out_dir>")
        return 1

    src, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)

    blobs = {}
    cur = None
    buf = bytearray()
    expect = 0

    with io.open(src, encoding="utf-8") as f:
        for line in f:
            if cur is None:
                m = START.search(line)
                if not m:
                    continue
                cur, expect, buf = m.group(1), int(m.group(2)), bytearray()
                line = line[m.end():]

            for h in BYTE.findall(line):
                buf.append(int(h, 16))

            if "};" in line:
                if len(buf) != expect:
                    raise SystemExit("size mismatch %s: %d != %d"
                                     % (cur, len(buf), expect))
                blobs[cur] = bytes(buf)
                cur = None

    # 表在文件末尾，单独扫一遍
    rows = []
    with io.open(src, encoding="utf-8") as f:
        for line in f:
            m = TABLE.search(line)
            if m:
                rows.append((m.group(1), m.group(2), int(m.group(3))))

    if not rows:
        raise SystemExit("没有在 rom.h 里找到 pb_roms[] 表")

    order = []
    total = 0
    for name, ident, size in rows:
        data = blobs.get(ident)
        if data is None:
            raise SystemExit("缺数组: %s" % ident)
        if len(data) != size:
            raise SystemExit("%s 长度与表不符" % name)

        # 文件名 -> 菜单名 的逆运算：gen_rom.py 的 nice_name 把 '_' 换回空格，
        # 所以这里把空格换成 '_'，重建后菜单文字与原来逐字符相同。
        stem = name.replace(" ", "_")
        ext = ".gbc" if (len(data) > 0x143 and (data[0x143] & 0x80)) else ".gb"
        fname = stem + ext

        path = os.path.join(out_dir, fname)
        with io.open(path, "wb") as f:
            f.write(data)
        order.append(fname)
        total += len(data)
        print("  %-52s %8d B  %s" % (fname, len(data), ext))

    with io.open(os.path.join(out_dir, "roms.list"), "w", encoding="utf-8",
                 newline="\n") as f:
        f.write("# Menu order: one file name per line. gen_gb_rom.py follows this\n")
        f.write("# list when present; delete this file to fall back to name order.\n")
        for fn in order:
            f.write(fn + "\n")

    print("=> %d 份 ROM，共 %d 字节，顺序表 roms.list 已写入" % (len(order), total))
    return 0


if __name__ == "__main__":
    sys.exit(main())
