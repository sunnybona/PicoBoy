/* rom_menu.h — GB 侧多 ROM 菜单入口
 *
 * 真正的绘制与按键循环在 BSP 的 pb_rom_menu.cpp（GB / NES 共用同一份），
 * 这里只是把「内嵌 ROM 表」这种 GB 特有数据结构适配成通用菜单项。
 */
#ifndef PB_GBC_MENU_H
#define PB_GBC_MENU_H

#include "pb_roms.h"

/* 显示列表并等用户确认，返回选中的下标；只有一个 ROM 时直接返回 0 */
int pb_rom_menu_select(const pb_rom_entry_t *roms, int count);

/* 选完到进游戏之间的过场：LOADING + 游戏名 */
void pb_rom_splash(int idx, const pb_rom_entry_t *roms);

#endif /* PB_GBC_MENU_H */
