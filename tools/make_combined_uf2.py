#!/usr/bin/env python3
# make_combined_uf2.py — 把 bootloader + 多个槽位应用的 .bin 合并成一个 UF2
#
# 用法：
#   python3 make_combined_uf2.py 输出.uf2 基址 偏移1 a.bin 偏移2 b.bin ...
# 例如：
#   python3 make_combined_uf2.py out.uf2 0x10000000 \
#       0x0      picoboy_boot.bin \
#       0x10000  picoboy_gbc.bin \
#       0x80000  pico_nes.bin
#
# 只对实际有数据的 256 字节块生成 UF2 块（槽位间隙不写），块号全局连续。
import struct
import sys

UF2_MAGIC0 = 0x0A324655
UF2_MAGIC1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILY = 0x00002000
RP2040_FAMILY_ID = 0xe48bff56
PAYLOAD = 256


def collect(base, parts):
    """返回 [(addr, 256 字节 chunk)]，按地址排序、去重叠。"""
    blocks = []
    for off, path in parts:
        data = open(path, "rb").read()
        for i in range(0, len(data), PAYLOAD):
            chunk = data[i:i + PAYLOAD]
            if len(chunk) < PAYLOAD:
                chunk = chunk + b"\xff" * (PAYLOAD - len(chunk))
            blocks.append((base + off + i, chunk))
    blocks.sort(key=lambda b: b[0])
    return blocks


def main():
    out = sys.argv[1]
    base = int(sys.argv[2], 0)
    args = sys.argv[3:]
    parts = []
    for i in range(0, len(args), 2):
        parts.append((int(args[i], 0), args[i + 1]))

    blocks = collect(base, parts)
    total = len(blocks)

    with open(out, "wb") as f:
        for no, (addr, chunk) in enumerate(blocks):
            hdr = struct.pack("<IIIIII",
                              UF2_MAGIC0, UF2_MAGIC1, UF2_FLAG_FAMILY,
                              addr, PAYLOAD, no)
            hdr2 = struct.pack("<II", total, RP2040_FAMILY_ID)
            f.write(hdr + hdr2 + chunk + b"\x00" * (476 - PAYLOAD))
            f.write(struct.pack("<I", UF2_MAGIC_END))

    print("wrote %s: %d blocks (%.1f KB payload)" %
          (out, total, total * PAYLOAD / 1024.0))


if __name__ == "__main__":
    main()
