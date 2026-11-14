#!/usr/bin/env python3
"""make_test_rom.py — 生成一份自制的 NES 测试 ROM（roms/pb_nes_test.nes）

用途：pico-nes 的空板自检。屏幕会变成 15x15 的彩色格子方块（每块 32x32
像素，内部再分 16x16 的四个象限），颜色依次是白 / 红 / 绿 / 蓝。

原理：CHR 全部为 0（所有图块像素索引都是 0），nametable 图块索引也全 0，
所以每个像素取的都是"该象限所选调色板的 0 号色"。属性表整片填 $E4
（TL=0 TR=1 BL=2 BR=3），于是 4 个 16x16 象限分别用 $3F00/$3F04/$3F08/
$3F0C 这 4 个颜色。这条路径刚好把 PPU 取图块 → 属性 → 调色板 → 逐行输出
全串起来，画面对就说明整条渲染与送屏链路是通的。

这只是自检工具，不是游戏。ROM 内容全部在下面用 6502 机器码拼出来。
"""

import os
import sys

ORIGIN = 0xC000          # 16KB PRG 的映射基址


class Asm:
    def __init__(self):
        self.buf = bytearray()
        self.labels = {}
        self.fixups = []   # (pos, kind, name)

    def pc(self):
        return len(self.buf)

    def label(self, name):
        assert name not in self.labels, name
        self.labels[name] = self.pc()

    def raw(self, *bs):
        self.buf.append(bs[0])
        for b in bs[1:]:
            self.buf.append(b)

    # --- 指令 ---
    def sei(self):  self.raw(0x78)
    def cld(self):  self.raw(0xD8)
    def txs(self):  self.raw(0x9A)
    def inx(self):  self.raw(0xE8)
    def dex(self):  self.raw(0xCA)
    def lda_imm(self, v):    self.raw(0xA9, v)
    def ldx_imm(self, v):    self.raw(0xA2, v)
    def cpx_imm(self, v):    self.raw(0xE0, v)
    def sta_abs(self, a):    self.raw(0x8D, a & 0xFF, a >> 8)
    def lda_absx(self, addr_ref):
        self.raw(0xBD, 0, 0)
        self.fixups.append((self.pc() - 2, "abs", addr_ref))
    def bit_abs(self, a):    self.raw(0x2C, a & 0xFF, a >> 8)
    def jmp(self, ref):
        self.raw(0x4C, 0, 0)
        self.fixups.append((self.pc() - 2, "abs", ref))
    def bne(self, ref):
        self.raw(0xD0, 0)
        self.fixups.append((self.pc() - 1, "rel", ref))
    def bpl(self, ref):
        self.raw(0x10, 0)
        self.fixups.append((self.pc() - 1, "rel", ref))

    def resolve(self):
        for pos, kind, name in self.fixups:
            tgt = self.labels[name]
            if kind == "rel":
                off = tgt - (pos + 1)
                assert -128 <= off <= 127, (name, off)
                self.buf[pos] = off & 0xFF
            else:
                addr = ORIGIN + tgt
                self.buf[pos] = addr & 0xFF
                self.buf[pos + 1] = addr >> 8

    def pad_to(self, n):
        while len(self.buf) < n:
            self.buf.append(0xEA)   # NOP


def build_prg():
    a = Asm()

    a.sei()
    a.cld()
    a.ldx_imm(0xFF)
    a.txs()

    a.lda_imm(0x00)
    a.sta_abs(0x2000)          # 关 NMI
    a.sta_abs(0x2001)          # 关渲染

    a.label("wait1")           # 等 PPU 上电稳定：两个 vblank
    a.bit_abs(0x2002)
    a.bpl("wait1")
    a.label("wait2")
    a.bit_abs(0x2002)
    a.bpl("wait2")

    # 调色板 $3F00..$3F1F
    a.lda_imm(0x3F)
    a.sta_abs(0x2006)
    a.lda_imm(0x00)
    a.sta_abs(0x2006)
    a.ldx_imm(0x00)
    a.label("palloop")
    a.lda_absx("paltab")
    a.sta_abs(0x2007)
    a.inx()
    a.cpx_imm(0x10)
    a.bne("palloop")

    # 属性表 $23C0..$23FF 全填 $E4
    a.lda_imm(0x23)
    a.sta_abs(0x2006)
    a.lda_imm(0xC0)
    a.sta_abs(0x2006)
    a.lda_imm(0xE4)
    a.ldx_imm(0x40)
    a.label("attrloop")
    a.sta_abs(0x2007)
    a.dex()
    a.bne("attrloop")

    a.lda_imm(0x0A)            # 显示背景（含左侧 8 像素）
    a.sta_abs(0x2001)

    a.label("hang")
    a.jmp("hang")

    a.pad_to(0x50)

    # 调色板表：$3F00 白、$3F04 红、$3F08 绿、$3F0C 蓝
    # 只写 $3F00-$3F0F 这 16 个字节，不碰 $3F10-$3F1F：在硬件上 $3F10 会
    # 镜像到 $3F00，而 InfoNES 实现镜像时会把 4 个"0 号色"一起改掉，写精灵
    # 那半边只会把背景色冲掉（第一次自检就踩了这个坑）。
    a.label("paltab")
    for base in (0x30, 0x16, 0x2A, 0x12):
        a.raw(*([base] * 4))

    a.resolve()

    prg = bytearray(a.buf)
    while len(prg) < 0x4000:
        prg.append(0xEA)
    assert len(prg) <= 0x4000, len(prg)

    # 向量表 $FFFA: NMI / RESET / IRQ
    hang = ORIGIN + a.labels["hang"]
    if 0x3FFA + 6 <= len(prg):
        for i, addr in enumerate((hang, ORIGIN, hang)):
            off = 0x3FFA + i * 2
            prg[off] = addr & 0xFF
            prg[off + 1] = addr >> 8
    return bytes(prg)


def build_rom():
    prg = build_prg()

    # CHR：只做 tile 0 —— plane0 全 1、plane1 全 0，于是每个像素的色号都是 1。
    # 不能用"全 0"图块：色号 0 在 NES 上是"背景色"，会取 $3F00（而且不走
    # 属性指定的调色板），四个象限就都一样了（第一次自检踩的第二个坑）。
    chr_ = bytearray(0x2000)
    chr_[0:8] = b"\xff" * 8                     # tile 0 的 plane 0

    header = b"NES\x1a" + bytes([1, 1, 0x00, 0x00]) + bytes(8)
    return header + prg + bytes(chr_)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join("roms", "pb_nes_test.nes")
    data = build_rom()
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, "wb") as fp:
        fp.write(data)
    print("=> %s (%d bytes, PRG 16K / CHR 8K, mapper 0)" % (out, len(data)))


if __name__ == "__main__":
    main()
