/*
 * rom_menu.cpp — GB 侧菜单适配层
 *
 * 菜单的绘制、按键手感、配色全部由 BSP 的 pb_rom_menu 负责（与 NES 同一份代码），
 * 这里只做三件事：
 *   1) 把 pb_flash_rom_entry_t 转换成共用菜单项：名字砍掉 "(USA)" 之类的区域后缀，
 *      信息行填 "容量 MBC 机型"
 *   2) 提供标题/副标题文案
 *   3) 装上 Y 键回调，进 BSP 的亮度/音量设置页
 *
 * 卡片 header 在菜单阶段还读不到模拟器状态（gb_init 尚未调用），所以 MBC 类型
 * 和 CGB 标志都直接从 ROM 的 0x147 / 0x143 字节读。
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "picoboy/pb_display.h"
#include "picoboy/pb_settings.h"
#include "picoboy/pb_rom_menu.h"
#include "picoboy/pb_flash_rom.h"

#include "rom_menu.h"

/* 2 倍字形从 x=14 起，模块内最多 27 字符（含 "序号 " 前缀），名字留 24 */
#define MENU_NAME_MAX 24

/* 一张卡一项；GB 这端按 32 张封顶，够用且静态分配不占堆 */
#define MENU_MAX_ROMS 32

static const pb_flash_rom_entry_t *g_roms;
static int g_count;

static pb_menu_item_t s_items[MENU_MAX_ROMS];
static char s_names[MENU_MAX_ROMS][MENU_NAME_MAX + 4];
static char s_infos[MENU_MAX_ROMS][24];
static char s_sub[32];

/* ---------------- ROM 信息 ---------------- */

static const char *mbc_name(uint8_t t)
{
    if(t == 0x00)              return "NO MBC";
    if(t >= 0x01 && t <= 0x03) return "MBC1";
    if(t >= 0x05 && t <= 0x06) return "MBC2";
    if(t >= 0x0F && t <= 0x13) return "MBC3";
    if(t >= 0x19 && t <= 0x1E) return "MBC5";
    return "OTHER";
}

/* 文件名 -> 菜单标题：砍掉 "(USA, Europe)" 这类区域后缀（字模没有括号和逗号，
 * 直接画会变成一串空格），太长就截断成 "..."。 */
static void rom_label(int idx, char *dst, int cap)
{
    const char *src = g_roms[idx].name;
    int n = 0;
    while(src[n] && src[n] != '(' && n < cap - 3)
    {
        dst[n] = src[n];
        n++;
    }
    while(n > 0 && dst[n - 1] == ' ') n--;
    if(src[n] && src[n] != '(')
    {
        dst[n++] = '.';
        dst[n++] = '.';
        dst[n++] = '.';
    }
    dst[n] = 0;
}

/* 组装共用菜单项 */
static void build_items(void)
{
    for(int i = 0; i < g_count; i++)
    {
        rom_label(i, s_names[i], MENU_NAME_MAX);

        const uint8_t *hdr = g_roms[i].data;
        bool cgb = (hdr[0x143] & 0x80) != 0;
        snprintf(s_infos[i], sizeof s_infos[i], "%uK  %s  %s",
                 (unsigned)(g_roms[i].size >> 10), mbc_name(hdr[0x147]),
                 cgb ? "CGB" : "DMG");

        s_items[i].name = s_names[i];
        s_items[i].info = s_infos[i];
    }
}

/* Y：进 BSP 设置页，返回后立刻落盘（此刻单核，擦 flash 不影响推屏） */
static void gb_setup(void)
{
    pb_settings_menu();
    pb_settings_save();
}

/* ---------------- 对外接口 ---------------- */

int pb_rom_menu_select(const pb_flash_rom_entry_t *roms, int count)
{
    if(count <= 0)
        return -1;

    g_roms  = roms;
    g_count = count > MENU_MAX_ROMS ? MENU_MAX_ROMS : count;

    build_items();

    snprintf(s_sub, sizeof s_sub, "%d ROMS  WALNUT-CGB", g_count);

    pb_menu_style_t style;
    style.title      = "PICOBOY GBC";
    style.subtitle   = s_sub;
    style.hint       = "A/X PLAY   Y SETUP";
    style.info_color = PB_CYAN;

    pb_rom_menu_set_setup_cb(gb_setup);

    return pb_rom_menu_select(&style, s_items, g_count);
}

void pb_rom_splash(int idx, const pb_flash_rom_entry_t *roms)
{
    if(idx < 0)
        return;

    g_roms = roms;
    char name[MENU_NAME_MAX + 4];
    rom_label(idx, name, MENU_NAME_MAX);
    pb_rom_menu_splash(name);
}
