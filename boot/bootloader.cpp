/*
 * bootloader.cpp — PicoBoy 开机引导
 *
 * 上电先出系统选择菜单（GBC / NES），确认后把控制权交给对应槽位里的应用镜像。
 * 两个应用各自是独立链接的完整固件（自带 CRT0、独立 bss），所以 RAM 占用和
 * 单固件时完全一致，不会因为同时容纳两个模拟器而溢出。
 *
 * flash 分区（16MB）：
 *   0x10000000  本 bootloader（64KB）
 *   0x10040000  GBC 槽（到 0x10080000）
 *   0x10080000  NES 槽（到 0x10120000）
 *   0x10120000  共享 ROM 区（rom_uploader 写入的 ustar tar，两应用同址直读）
 *
 * 跳转：应用镜像开头是 256 字节 boot2，其后是向量表（初始 SP、reset 向量）。
 * 这里设置 VTOR 指向应用向量表、载入其初始 MSP，再 bx 到 reset handler；
 * 应用 CRT0 随后清零 bss、拷贝 data，进入它自己的 main。
 */
#include "pico/stdlib.h"

#include <stdio.h>

#include "picoboy/board.h"
#include "picoboy/pb_display.h"
#include "picoboy/pb_audio.h"
#include "picoboy/pb_input.h"
#include "picoboy/pb_settings.h"
#include "picoboy/pb_rom_menu.h"

#include "hardware/regs/addressmap.h"
#include "hardware/regs/m0plus.h"

/* ---- flash 分区地址 ---- */
#define GBC_SLOT_ADDR   0x10040000u
#define GBC_SLOT_END    0x10080000u
#define NES_SLOT_ADDR   0x10080000u
#define NES_SLOT_END    0x10120000u

/* 每个应用镜像里 boot2 占 256 字节，向量表紧随其后 */
#define APP_VTOR_OFF    0x100u

/* 判断槽位里是否有可跳转的应用：向量表的初始 SP 必须落在 SRAM，
 * reset 向量（thumb）必须指向本槽范围内。空 flash 全是 0xFF，会被挡掉。 */
static bool app_present(uint32_t slot, uint32_t end)
{
    uint32_t vt = slot + APP_VTOR_OFF;
    uint32_t sp = *(volatile uint32_t *)(vt + 0x0);
    uint32_t pc = *(volatile uint32_t *)(vt + 0x4);

    if(sp < 0x20000000u || sp > 0x20042000u)
        return false;
    if((pc & 1u) == 0u)
        return false;

    uint32_t target = pc & ~1u;
    return target >= slot && target < end;
}

/* 关中断 → 设 VTOR → 载 MSP → bx 到应用 reset handler，不返回 */
static void __attribute__((noreturn))
jump_to_app(uint32_t slot)
{
    uint32_t vt = slot + APP_VTOR_OFF;
    uint32_t sp = *(volatile uint32_t *)(vt + 0x0);
    uint32_t pc = *(volatile uint32_t *)(vt + 0x4);

    __asm volatile ("cpsid i" ::: "memory");
    *(volatile uint32_t *)(PPB_BASE + M0PLUS_VTOR_OFFSET) = vt;

    __asm volatile (
        "msr msp, %0\n"
        "bx  %1\n"
        : : "r"(sp), "r"(pc) : "memory");
    __builtin_unreachable();
}

static void fatal(const char *msg)
{
    pb_clear(PB_BLACK);
    pb_text_center(100, "PICOBOY", 2, PB_YELLOW, PB_BLACK);
    pb_text_center(126, msg, 1, PB_RED, PB_BLACK);
    pb_display_flush();
    while(true)
        tight_loop_contents();
}

/* ---- 开机菜单里的反色开关（Y 键）----
 * 即时翻转面板反色并落盘：跳转后的应用 pb_settings_init 会读到同一份，
 * 不用进游戏再改。提示串同步显示当前 ON/OFF。 */
static char s_hint_buf[28];

static void refresh_hint(void)
{
    const char *st = pb_settings_get()->invert ? "ON" : "OFF";
    snprintf(s_hint_buf, sizeof s_hint_buf, "A START  Y INVERT:%s", st);
}

static void toggle_invert(void)
{
    uint8_t nv = pb_settings_get()->invert ? 0u : 1u;
    pb_settings_set_invert(nv);
    pb_settings_save();
    refresh_hint();
}

int main(void)
{
    set_sys_clock_khz(PB_SYS_CLOCK_KHZ, true);
    stdio_init_all();

    pb_display_init();
    pb_audio_init();
    pb_input_init();

    /* 亮度/音量沿用 flash 里的设置，避免开机屏幕过暗 */
    pb_settings_init();

    /* 只列出实际存在的应用，并记下各自槽位地址 */
    pb_menu_item_t items[2];
    uint32_t       slots[2];
    int n = 0;

    if(app_present(GBC_SLOT_ADDR, GBC_SLOT_END))
    {
        items[n].name = "GBC";
        items[n].info = "GAME BOY COLOR";
        slots[n] = GBC_SLOT_ADDR;
        ++n;
    }
    if(app_present(NES_SLOT_ADDR, NES_SLOT_END))
    {
        items[n].name = "NES";
        items[n].info = "NINTENDO 8-BIT";
        slots[n] = NES_SLOT_ADDR;
        ++n;
    }

    if(n == 0)
        fatal("NO APP IN FLASH");

    refresh_hint();

    pb_menu_style_t style;
    style.title      = "PICOBOY";
    style.subtitle   = "SELECT SYSTEM";
    style.hint       = s_hint_buf;
    style.info_color = PB_CYAN;

    pb_rom_menu_set_setup_cb(toggle_invert);
    int sel = pb_rom_menu_select(&style, items, n);
    if(sel < 0)
        fatal("MENU ERROR");

    jump_to_app(slots[sel]);
}
