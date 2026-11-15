/*
 * pb_nes.h — pico-nes 的公共配置与 ROM 表结构
 *
 * 竖屏（240 宽）：NES 256x240 左右各裁 8 列，取中间 240 列 1:1 显示。
 * 横屏（240x320 旋转成 320x240）：完整保留 256 列，由 BSP 水平居中，不裁剪。
 */
#ifndef PB_NES_H
#define PB_NES_H

#include <stdint.h>

#include "picoboy/board.h"
#include "picoboy/pb_input.h"

/* 送屏可视区：横屏用完整 256 列；竖屏取中间 240 列 */
#define PB_NES_SRC_W   256
#if PB_LCD_ROTATE
#define PB_NES_CROP_X  0
#define PB_NES_VIEW_W  256
#else
#define PB_NES_CROP_X  8
#define PB_NES_VIEW_W  240
#endif
#define PB_NES_VIEW_H  240

/* 一帧节拍：2C02 是 60.0988Hz，约 16639us。和 NES 真实帧率一致，
 * 不快进也不拖慢，音乐音高才对得上。 */
#define PB_NES_FRAME_US 16639

/* 选中 ROM 的键 */
#define PB_NES_KEY_OK   (PB_BTN_A)

/* 一份 NES ROM 描述（data 指向 flash ROM 区，由 pb_flash_rom 扫出的条目构造） */
typedef struct
{
    const char    *name;      /* 显示名（来自 tar 文件名） */
    uint8_t        mapper;    /* iNES mapper 号 */
    uint16_t       prg_kb;    /* PRG ROM 大小（KiB） */
    uint16_t       chr_kb;    /* CHR ROM 大小（KiB），0 表示 CHR RAM */
    const uint8_t *data;      /* 完整 .nes 文件内容（含 16 字节头），位于 flash */
    uint32_t       size;
} pb_nes_rom_t;

/* 交给平台层：下一次 InfoNES_Load() 用哪一份 ROM */
void pb_nes_set_rom(const pb_nes_rom_t *rom);

#endif /* PB_NES_H */
