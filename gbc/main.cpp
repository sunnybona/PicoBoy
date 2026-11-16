/*
 * main.cpp — PicoBoy GBC：Walnut-CGB 核心 + PicoBoy BSP
 *
 * 分工：
 *   Walnut-CGB (walnut_cgb.h) 负责 DMG/CGB CPU、显存、调色板、游戏逻辑；
 *   minigb_apu 负责 Game Boy APU 声音；
 *   PicoBoy BSP 负责 ST7789 显示（PIO+DMA）、PWM 音频、按键；
 *   rom_menu.cpp 负责多 ROM 选择菜单。
 *
 * 显示（与花生 DMG 版同一套几何）：
 *   Walnut-CGB 每帧调 144 次 lcd_draw_line，本文件把每条线翻成 160 个 RGB565
 *   颜色（CGB 直接查 gb->cgb.fixPalette[]，DMG 走四档绿调色板），再交给
 *   pb_push_rgb_line() 做 1.5 倍最近邻放大（240x216，y=12 居中）。
 *   core0 跑模拟并填带，core1 由 pb_display_start_pusher() 启动，专门推屏。
 *
 * 按键：BSP 的位序与 Walnut 的 JOYPAD_* 一致，直接取反写入 direct.joypad。
 */
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/watchdog.h"

#include "picoboy/pb_display.h"
#include "picoboy/pb_audio.h"
#include "picoboy/pb_input.h"
#include "picoboy/pb_settings.h"      /* 亮度/音量：flash 持久化 + 设置页（BSP 提供） */

#define ENABLE_SOUND 1
#define ENABLE_LCD   1
#if ENABLE_SOUND
/* walnut_cgb.h 在 ENABLE_SOUND=1 时会调用这两个全局函数，需先声明 */
uint8_t audio_read(uint16_t addr);
void audio_write(uint16_t addr, uint8_t val);
#endif
/* walnut_cgb.h 内部的原型里第一次出现 struct gb_s（只声明在原型作用域），
 * 这里先做文件作用域的前置声明，避免和后面真正的结构体定义冲突。 */
struct gb_s;
#include "walnut_cgb.h"

#include "minigb_apu/minigb_apu.h"
#include "picoboy/pb_flash_rom.h"   /* flash ROM 区 ustar tar 扫描 */
#include "rom_menu.h"

/* 卡带 RAM：CGB 常见上限 4 个 8KB bank = 32KB（宝可梦水晶等） */
#define CART_RAM_SIZE 32768
static uint8_t cart_ram[CART_RAM_SIZE];

static struct gb_s gb;
static struct minigb_apu_ctx apu;

/* 当前选中的 ROM：指向 flash（XIP 直读），不做 RAM 拷贝 */
static const uint8_t *g_rom_data;
static uint32_t       g_rom_size;

/* ---------------- ROM 读取 ---------------- */
/* 按真实大小做边界，越界返回 0xFF（GB ROM 多为 2 的幂，边界判断同样安全）。 */
static inline uint8_t rom_byte(uint32_t addr)
{
    return addr < g_rom_size ? g_rom_data[addr] : 0xFF;
}

static uint8_t gb_rom_read(struct gb_s *gbp, const uint_fast32_t addr)
{
    (void)gbp;
    return rom_byte((uint32_t)addr);
}

static uint16_t gb_rom_read16(struct gb_s *gbp, const uint_fast32_t addr)
{
    (void)gbp;
    /* Cortex-M0+ 不支持非对齐半字访问，这里显式拼两个字节 */
    return (uint16_t)(rom_byte((uint32_t)addr) |
                      (rom_byte((uint32_t)addr + 1u) << 8));
}

static uint32_t gb_rom_read32(struct gb_s *gbp, const uint_fast32_t addr)
{
    (void)gbp;
    return (uint32_t)rom_byte((uint32_t)addr) |
           ((uint32_t)rom_byte((uint32_t)addr + 1u) << 8) |
           ((uint32_t)rom_byte((uint32_t)addr + 2u) << 16) |
           ((uint32_t)rom_byte((uint32_t)addr + 3u) << 24);
}

/* ---------------- 卡带 RAM ---------------- */
static uint8_t gb_cart_ram_read(struct gb_s *gbp, const uint_fast32_t addr)
{
    (void)gbp;
    return addr < CART_RAM_SIZE ? cart_ram[addr] : 0xFF;
}

static void gb_cart_ram_write(struct gb_s *gbp, const uint_fast32_t addr,
                              const uint8_t val)
{
    (void)gbp;
    if(addr < CART_RAM_SIZE)
        cart_ram[addr] = val;
}

/* ---------------- 错误处理 ---------------- */
static void gb_error(struct gb_s *gbp, const enum gb_error_e err,
                     const uint16_t addr)
{
    (void)gbp; (void)addr;
    pb_clear(PB_BLACK);
    pb_text(4, 100, err == GB_INVALID_OPCODE ? "BAD OPCODE"
                                              : "EMULATION ERR",
            PB_RED, PB_BLACK);
    pb_display_flush();
    while(true)
        tight_loop_contents();
}

/* ---------------- APU 挂钩 ---------------- */
uint8_t audio_read(uint16_t addr)
{
    return minigb_apu_audio_read(&apu, addr);
}

void audio_write(uint16_t addr, uint8_t val)
{
    minigb_apu_audio_write(&apu, addr, val);
}

/* ---------------- 逐行显示回调 ---------------- */
/* DMG 四档绿（与 BSP 默认一致）：0 最亮 -> 3 最暗 */
static const uint16_t s_dmg_pal[4] = {
    PB_RGB(0x9B, 0xBC, 0x0F),
    PB_RGB(0x8B, 0xAC, 0x0F),
    PB_RGB(0x30, 0x62, 0x30),
    PB_RGB(0x0F, 0x38, 0x0F),
};

static uint16_t s_line[LCD_WIDTH];

static void lcd_draw_line(struct gb_s *gbp, const uint8_t pixels[LCD_WIDTH],
                          const uint_fast8_t line)
{
    if(gbp->cgb.cgbMode)
    {
        /* CGB：像素值就是 fixPalette 的下标（BG 0x00-0x1F / OBJ 0x20-0x3F），
         * fixPalette 里已经是转换好的 RGB565。 */
        for(int x = 0; x < LCD_WIDTH; x++)
            s_line[x] = gbp->cgb.fixPalette[pixels[x] & 0x3F];
    }
    else
    {
        /* DMG（含在 CGB 上跑 DMG 游戏）：只用低两位的灰度档，统一绿屏 */
        for(int x = 0; x < LCD_WIDTH; x++)
            s_line[x] = s_dmg_pal[pixels[x] & 3];
    }
    pb_push_rgb_line(s_line, (uint8_t)line);
}

/* ---------------- 按键 ---------------- */
/* hram_io 下标（0xFF00 起的偏移） */
#define IO_IDX_JOYP 0x00
#define IO_IDX_IF   0x0F
#define IO_IDX_IE   0xFF

/* 每帧合并按键：pb_input_read 返回 1=按下；Walnut 的 direct.joypad 低有效
 * （0=按下），所以取反。同时按当前选中半区重算 P1，并每帧只补一次手柄中断。 */
static void update_pad(void)
{
    uint8_t pad = pb_input_read();
    gb.direct.joypad = (uint8_t)~pad;

    uint8_t sel = gb.hram_io[IO_IDX_JOYP] & 0x30;
    uint8_t lo = 0x0F;
    if(!(sel & 0x10)) lo &= (uint8_t)~(pad >> 4);    /* 方向键 */
    if(!(sel & 0x20)) lo &= (uint8_t)~(pad & 0x0F);  /* AB/Start/Select */
    gb.hram_io[IO_IDX_JOYP] = (uint8_t)(0xC0 | sel | lo);

    /* 手柄中断（IF bit4）：按住期间每帧只补一次，松手即停 */
    if(pad && (gb.hram_io[IO_IDX_IE] & 0x10))
        gb.hram_io[IO_IDX_IF] |= 0x10;
}

/* ---------------- flash ROM 校验 ---------------- */
/* Nintendo logo（0x104..0x133 的前 16 字节足够判定） */
static const uint8_t k_logo_sig[] = {
    0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B,
    0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D};

static bool check_gb_logo(const uint8_t *d)
{
    for(uint32_t i = 0; i < sizeof(k_logo_sig); i++)
        if(d[0x104 + i] != k_logo_sig[i])
            return false;
    return true;
}

/* 头校验和（0x134..0x14C），与 logo 任一成立即算合法 */
static bool check_gb_header_checksum(const uint8_t *d)
{
    int chk = 0;
    for(int i = 0x134; i <= 0x14C; i++)
        chk = chk - d[i] - 1;
    return (chk & 0xFF) == (int)d[0x14D];
}

static bool gb_rom_validator(const uint8_t *data, uint32_t size)
{
    if(size < 0x150) return false;
    return check_gb_logo(data) || check_gb_header_checksum(data);
}

/* ---------------- 错误页 ---------------- */
static void fatal(const char *msg)
{
    pb_clear(PB_BLACK);
    pb_text(4, 90, "PICOBOY GBC", PB_YELLOW, PB_BLACK);
    pb_text(4, 110, msg, PB_RED, PB_BLACK);
    pb_display_flush();
    while(true)
        tight_loop_contents();
}

int main(void)
{
    /* 先超频，再初始化依赖时钟的 BSP（PWM 载波 = 250MHz/4096 ≈ 61kHz） */
    set_sys_clock_khz(PB_SYS_CLOCK_KHZ, true);
    stdio_init_all();

    pb_display_init();
    pb_audio_init();
    pb_input_init();

    /* 亮度和音量来自 flash 里存的设置（没存过就用默认：9 档亮 / 8 档音量） */
    pb_settings_init();

    /* 扫描 flash ROM 区（rom_uploader 写入的 tar），静态数组不占堆 */
    static pb_flash_rom_entry_t roms[32];
    int rom_count = pb_flash_rom_scan(roms, 32, gb_rom_validator);

    /* 多 ROM 才有菜单；只有一个时直接进游戏 */
    int idx = pb_rom_menu_select(roms, rom_count);
    if(idx < 0)
        fatal("NO ROM IN FLASH");

    g_rom_data = roms[idx].data;
    g_rom_size = roms[idx].size;

    pb_rom_splash(idx, roms);

    enum gb_init_error_e ret = gb_init(&gb, &gb_rom_read, &gb_rom_read16,
                                       &gb_rom_read32, &gb_cart_ram_read,
                                       &gb_cart_ram_write, &gb_error, NULL);
    if(ret != GB_INIT_NO_ERROR)
    {
        if(ret == GB_INIT_CARTRIDGE_UNSUPPORTED)
            fatal("UNSUPPORTED CARTRIDGE");
        else if(ret == GB_INIT_INVALID_CHECKSUM)
            fatal("ROM HEADER CHECKSUM BAD");
        else
            fatal("GB INIT FAILED");
    }

    gb_init_lcd(&gb, &lcd_draw_line);
    minigb_apu_audio_init(&apu);

    /* GBC 要的是真实色，面板反色开关由 board.h 的 PB_LCD_INVERT 决定
     * （本板 1 = 面板自己会发反色，初始化时已发 0x21 抵消）。 */
    pb_display_start_pusher();

    /* APU 输出：立体声交错，再混成单声道。
     * AUDIO_SAMPLES 是 (61035/59.7275)≈1021 的浮点表达式，不是整型常量表达式，
     * 不能直接当数组维度，这里按上限开静态缓冲。 */
    static int16_t pcm[1024 * 2];
    static int16_t mono[1024];

    /* 帧节奏：GB 原生 59.7275 fps = 16742.7us。不锁帧的话模拟会跑飞、
     * 音频环形缓冲也会一直丢样本。 */
    const uint64_t frame_us = 16743;
    uint64_t next_us = time_us_64();

    /* A+B+X+Y 四键同按约 1 秒：软复位回到 bootloader 选择菜单 */
    int exit_hold = 0;

    while(true)
    {
        pb_input_poll();
        update_pad();

        if((pb_input_read() & (PB_BTN_A | PB_BTN_B | PB_BTN_X | PB_BTN_Y))
           == (PB_BTN_A | PB_BTN_B | PB_BTN_X | PB_BTN_Y))
        {
            if(++exit_hold >= 60)
                watchdog_reboot(0, 0, 0);
        }
        else
        {
            exit_hold = 0;
        }

        pb_display_begin_frame();
        gb_run_frame_dualfetch(&gb);
        pb_display_end_frame();

        minigb_apu_audio_callback(&apu, pcm);
        for(int i = 0; i < (int)AUDIO_SAMPLES; i++)
            mono[i] = (int16_t)(((int32_t)pcm[i * 2] + (int32_t)pcm[i * 2 + 1]) >> 1);
        pb_audio_submit(mono, AUDIO_SAMPLES);

        next_us += frame_us;
        uint64_t now = time_us_64();
        if(now < next_us)
        {
            while(time_us_64() < next_us)
                tight_loop_contents();
        }
        else if(now > next_us + frame_us * 2u)
        {
            /* 落后超过两帧（例如刚进游戏/卡顿）就重新对齐，避免追赶式狂跑 */
            next_us = now;
        }
    }
}
