/*
 * main.cpp — pico-nes 主程序
 *
 * 上电 → 选 ROM（多份时出菜单）→ 跑 InfoNES，一帧一帧地推。
 *
 * 双核分工：
 *   core0：K6502 + PPU 逐行渲染 + APU 混音（InfoNES_RunFrame() 正好跑一帧）
 *   core1：BSP 的推屏 worker，把 60 行一条的带搬上屏（4 条带乒乓）
 */
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/watchdog.h"

#include "picoboy/board.h"
#include "picoboy/pb_display.h"
#include "picoboy/pb_input.h"
#include "picoboy/pb_audio.h"
#include "picoboy/pb_settings.h"      /* 亮度/音量：flash 持久化 + 设置页（BSP 提供） */
#include "picoboy/pb_rom_menu.h"      /* ROM 菜单：与 GB 工程共用同一份渲染/交互 */

#include "InfoNES.h"
#include "InfoNES_System.h"

#include "pb_nes.h"
#include "picoboy/pb_flash_rom.h"

/*-------------------------------------------------------------------*/
/*  小工具                                                            */
/*-------------------------------------------------------------------*/
/* 内置 3x5 字体只认 0-9 / A-Z / 少量符号，这里把文件名转成能画的样子 */
static void draw_text( int x, int y, const char *s, uint8_t scale,
                       uint16_t fg, uint16_t bg )
{
    char buf[ 40 ];
    int  n = 0;

    for ( ; *s && n < 29; ++s )
    {
        char c = *s;
        if ( c >= 'a' && c <= 'z' ) c = ( char )( c - 'a' + 'A' );
        if ( !( ( c >= '0' && c <= '9' ) || ( c >= 'A' && c <= 'Z' ) ||
               c == '.' || c == ':' || c == '-' || c == '/' || c == '_' ||
               c == '?' || c == '!' || c == '+' || c == ' ' ) )
            c = ' ';
        buf[ n++ ] = c;
    }
    buf[ n ] = 0;

    pb_text_scaled( x, y, buf, scale, fg, bg );
}

/*-------------------------------------------------------------------*/
/*  小工具：等键松开                                                    */
/*-------------------------------------------------------------------*/
/* 亮度/音量设置页已经下沉到 BSP：picoboy_bsp/src/pb_settings.cpp。
 * 这里只留菜单切换时用的 等这个键真的松开。
 * 从菜单按 Y 进设置页、从设置页按 A 退回菜单，同一个按键会被下一层立刻
 * 再判一次 just_pressed，不等释放就会出现进去就退出 / 退回就启动。 */
static void wait_keys_released( uint8_t mask )
{
    for ( ;; )
    {
        pb_input_poll();
        if ( !( pb_input_read() & mask ) )
            return;
        sleep_ms( 5 );
    }
}



/*-------------------------------------------------------------------*/
/*  ROM 菜单                                                          */
/*-------------------------------------------------------------------*/
#define PB_MENU_MAX_ROMS 32

static pb_flash_rom_entry_t s_flash[ PB_MENU_MAX_ROMS ];
static pb_nes_rom_t        s_roms[ PB_MENU_MAX_ROMS ];
static pb_menu_item_t      s_items[ PB_MENU_MAX_ROMS ];
static char s_infos[ PB_MENU_MAX_ROMS ][ 32 ];
static char s_sub[ 32 ];

/* flash ROM 校验：iNES 魔数 "NES\x1a" */
static bool nes_rom_validator( const uint8_t *data, uint32_t size )
{
    return size >= 16 &&
           data[ 0 ] == 'N' && data[ 1 ] == 'E' &&
           data[ 2 ] == 'S' && data[ 3 ] == 0x1a;
}

/* 从 iNES 头解析出 pb_nes_rom_t（data 仍指 flash，不拷贝） */
static void parse_nes_rom( int i )
{
    const uint8_t *d = s_flash[ i ].data;

    s_roms[ i ].name   = s_flash[ i ].name;
    s_roms[ i ].mapper = ( uint8_t )( ( d[ 6 ] >> 4 ) | ( d[ 7 ] & 0xF0 ) );
    s_roms[ i ].prg_kb = ( uint16_t )( d[ 4 ] * 16u );
    s_roms[ i ].chr_kb = ( uint16_t )( d[ 5 ] * 8u );
    s_roms[ i ].data   = d;
    s_roms[ i ].size   = s_flash[ i ].size;
}

/* Y：进 BSP 设置页，返回后落盘（此刻单核，擦 flash 不影响推屏） */
static void nes_setup( void )
{
    pb_settings_menu();
    pb_settings_save();
}

static int rom_menu( int n )
{
    for ( int i = 0; i < n; ++i )
    {
        parse_nes_rom( i );

        char chr_txt[ 8 ];
        /* chr_kb == 0 表示卡带里没有 CHR ROM，PPU 用板载 8KB CHR RAM */
        if ( s_roms[ i ].chr_kb == 0 )
            strcpy( chr_txt, "RAM" );
        else
            snprintf( chr_txt, sizeof chr_txt, "%dK", ( int )s_roms[ i ].chr_kb );

        snprintf( s_infos[ i ], sizeof s_infos[ i ],
                  "MAPPER %d  PRG %dK  CHR %s",
                  ( int )s_roms[ i ].mapper,
                  ( int )s_roms[ i ].prg_kb, chr_txt );

        s_items[ i ].name = s_roms[ i ].name;
        s_items[ i ].info = s_infos[ i ];
    }

    snprintf( s_sub, sizeof s_sub, "%d ROMS  INFONES", n );

    /* 外观（配色 / 字号 / 行距 / 高亮条 / 箭头 / 连发手感）由 BSP 统一提供，
     * 与 GB 工程逐像素一致；这里只负责文案和每项的附加信息。 */
    pb_menu_style_t style;
    style.title      = "PICOBOY NES";
    style.subtitle   = s_sub;
    style.hint       = "A/X PLAY   Y SETUP";
    style.info_color = PB_CYAN;

    pb_rom_menu_set_setup_cb( nes_setup );

    return pb_rom_menu_select( &style, s_items, n );
}

/*-------------------------------------------------------------------*/
/*  启动画面                                                          */
/*-------------------------------------------------------------------*/
static void show_message( const char *line1, const char *line2 )
{
    pb_clear( PB_RGB( 8, 8, 16 ) );
    draw_text( 8, 96, line1, 2, PB_RGB( 0xF8, 0xC8, 0x38 ), PB_RGB( 8, 8, 16 ) );
    if ( line2 )
        draw_text( 8, 130, line2, 1, PB_RGB( 190, 190, 200 ), PB_RGB( 8, 8, 16 ) );
    pb_display_flush();
}

/*-------------------------------------------------------------------*/
/*  main                                                              */
/*-------------------------------------------------------------------*/
int main( void )
{
    /* 250MHz：NES 每帧要跑 2.98 万条 CPU 周期 + 240 行渲染，超频是必须的 */
    set_sys_clock_khz( PB_SYS_CLOCK_KHZ, true );
    stdio_init_all();

    pb_display_init();
    pb_audio_init();
    pb_input_init();

    /* 亮度和音量来自 flash 里存的设置（没存过就用默认：9 档亮 / 8 档音量） */
    pb_settings_init();

    /* 扫描 flash ROM 区（rom_uploader 写入的 tar），静态数组不占堆 */
    int rom_count = pb_flash_rom_scan( s_flash, PB_MENU_MAX_ROMS,
                                       nes_rom_validator );

    int idx = rom_menu( rom_count );
    if ( idx < 0 )
    {
        show_message( "NO ROM", "USE rom_uploader TO FLASH .NES" );
        for ( ;; )
        {
            pb_input_poll();
            if ( pb_input_just_pressed( PB_BTN_Y ) )
            {
                pb_settings_menu();
                pb_settings_save();
                wait_keys_released( PB_BTN_A | PB_BTN_B | PB_BTN_Y );
                show_message( "NO ROM", "USE rom_uploader TO FLASH .NES" );
            }
            sleep_ms( 20 );
        }
    }

    pb_nes_set_rom( &s_roms[ idx ] );

    /* 与 GB 工程一致：进游戏前先给 LOADING + 游戏名 */
    pb_rom_menu_splash( s_roms[ idx ].name );

    pb_clear( PB_BLACK );
    pb_display_flush();

    printf( "pico-nes: %s (mapper %d)\n", s_roms[ idx ].name,
            ( int )s_roms[ idx ].mapper );

    InfoNES_Init();

    if ( InfoNES_Load( "embedded" ) != 0 )
    {
        show_message( "ROM ERROR", "UNSUPPORTED OR CORRUPT .NES" );
        for ( ;; )
        {
            pb_input_poll();
            if ( pb_input_just_pressed( PB_BTN_Y ) )
            {
                pb_settings_menu();
                pb_settings_save();
                wait_keys_released( PB_BTN_A | PB_BTN_B | PB_BTN_Y );
                show_message( "ROM ERROR", "UNSUPPORTED OR CORRUPT .NES" );
            }
            sleep_ms( 20 );
        }
    }

    /* 从这里开始 pb_fb 被带池接管，不能再当帧缓冲画东西了 */
    pb_display_start_pusher();
    pb_display_nes_begin_frame();

    /* 复位：X+Y（即 NES 的 Start+Select）按住约 1 秒 */
    int reset_hold = 0;
    /* A+B+X+Y 四键同按约 1 秒：软复位回 bootloader 选择菜单 */
    int exit_hold = 0;

    uint64_t next_us = time_us_64() + PB_NES_FRAME_US;

    for ( ;; )
    {
        pb_input_poll();
        uint8_t btns = pb_input_read();

        /* 四键同按优先级最高，先于 X+Y 软复位，避免进游戏又立刻复位 */
        if ( ( btns & ( PB_BTN_A | PB_BTN_B | PB_BTN_X | PB_BTN_Y ) ) ==
             ( PB_BTN_A | PB_BTN_B | PB_BTN_X | PB_BTN_Y ) )
        {
            reset_hold = 0;
            if ( ++exit_hold >= 60 )
                watchdog_reboot( 0, 0, 0 );
        }
        else
        {
            exit_hold = 0;

            if ( ( btns & ( PB_BTN_X | PB_BTN_Y ) ) ==
                 ( PB_BTN_X | PB_BTN_Y ) )
            {
                if ( ++reset_hold >= 60 )
                {
                    printf( "pico-nes: soft reset\n" );
                    InfoNES_Reset();
                    reset_hold = 0;
                }
            }
            else
            {
                reset_hold = 0;
            }
        }

        pb_display_nes_begin_frame();

        if ( InfoNES_RunFrame() < 0 )
            break;                      /* 核心要求退出（目前不会发生） */

        /* 帧节拍：2C02 60.0988Hz */
        uint64_t now = time_us_64();
        if ( now < next_us )
        {
            while ( time_us_64() < next_us ) tight_loop_contents();
            next_us += PB_NES_FRAME_US;
        }
        else
        {
            next_us += PB_NES_FRAME_US;
            if ( next_us < now )        /* 落后一帧以上就别补了，重新起算 */
                next_us = now + PB_NES_FRAME_US;
        }
    }

    return 0;
}
