/* rom_menu.h — GB 侧多 ROM 菜单入口
 *
 * 真正的绘制与按键循环在 BSP 的 pb_rom_menu.cpp（GB / NES 共用同一份），
 * 这里把 flash ROM 区扫出的条目适配成通用菜单项。
 */
#ifndef PB_GBC_MENU_H
#define PB_GBC_MENU_H

#include "picoboy/pb_flash_rom.h"

/* 显示列表并等用户确认，返回选中的下标；只有一个 ROM 时直接返回 0 */
int pb_rom_menu_select(const pb_flash_rom_entry_t *roms, int count);

/* 选完到进游戏之间的过场：LOADING + 游戏名 */
void pb_rom_splash(int idx, const pb_flash_rom_entry_t *roms);

#endif /* PB_GBC_MENU_H */
