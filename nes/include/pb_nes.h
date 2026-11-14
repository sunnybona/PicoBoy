/*
 * pb_nes.h — pico-nes 的公共配置与 ROM 表结构
 *
 * 屏幕是 240x240，NES 可见区是 256x240，所以左右各裁掉 8 列做 1:1 显示
 * （不缩放，省 CPU，也避免非整数缩放糊掉像素）。
 */
#ifndef PB_NES_H
#define PB_NES_H

#include <stdint.h>

#include "picoboy/pb_input.h"

/* 送屏可视区：256 列里取中间 240 列 */
#define PB_NES_SRC_W   256
#define PB_NES_CROP_X  8
#define PB_NES_VIEW_W  240
#define PB_NES_VIEW_H  240

/* 一帧节拍：2C02 是 60.0988Hz，约 16639us。和 NES 真实帧率一致，
 * 不快进也不拖慢，音乐音高才对得上。 */
#define PB_NES_FRAME_US 16639

/* 选中 ROM 的键 */
#define PB_NES_KEY_OK   (PB_BTN_A)

/* 一份内嵌 ROM（由 tools/gen_nes_rom.py 生成到 nes_rom.h） */
typedef struct
{
    const char    *name;      /* 显示名（大写 ASCII，来自文件名） */
    uint8_t        mapper;    /* iNES mapper 号 */
    uint16_t       prg_kb;    /* PRG ROM 大小（KiB） */
    uint16_t       chr_kb;    /* CHR ROM 大小（KiB），0 表示 CHR RAM */
    const uint8_t *data;      /* 完整 .nes 文件内容（含 16 字节头），位于 flash */
    uint32_t       size;
} pb_nes_rom_t;

/* 交给平台层：下一次 InfoNES_Load() 用哪一份 ROM */
void pb_nes_set_rom(const pb_nes_rom_t *rom);

#endif /* PB_NES_H */
