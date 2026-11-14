/* pb_roms.h — 内嵌 ROM 描述表的数据类型
 *
 * 单独成文件是为了让「菜单」这类只需要表结构、不需要 ROM 数据的编译单元
 * （rom_menu.cpp、PC 端预览工具）不必把几 MB 的 rom.h 再解析一遍。
 * gen_rom.py 生成的 rom.h 会 include 本文件。
 */
#ifndef PB_ROMS_H
#define PB_ROMS_H

#include <stdint.h>

typedef struct {
    const char *name;      /* 菜单显示名（来自文件名） */
    const uint8_t *data;   /* 指向 flash 内 ROM */
    uint32_t size;
} pb_rom_entry_t;

#endif /* PB_ROMS_H */
