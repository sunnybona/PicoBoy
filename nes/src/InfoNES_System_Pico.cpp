/*
 * InfoNES_System_Pico.cpp — InfoNES 平台层（PicoBoy / RP2040）
 *
 * 上游 InfoNES 把"系统相关"的东西集中在 InfoNES_System.h 里，这里全部落到
 * PicoBoy BSP 上：
 *   - 显示：InfoNES 逐行渲染（WorkFrame 是单行缓冲），每行查表转 RGB565
 *           后交给 pb_push_nes_line()，由 core1 负责搬上屏。
 *   - 输入：8 个按键映射到 NES 手柄 1。
 *   - 音频：pAPU 每帧产出 735 个 44.1kHz 样本，这里重采样到 BSP 的
 *           61035Hz PWM 载波再交给 pb_audio_submit()。
 *   - ROM：直接指向 flash 里的 const 数组（XIP 读），不占 RAM。
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "pico/stdlib.h"

#include "picoboy/pb_display.h"
#include "picoboy/pb_input.h"
#include "picoboy/pb_audio.h"

#include "InfoNES.h"
#include "InfoNES_System.h"

#include "pb_nes.h"

/*===================================================================*/
/*  调色板                                                           */
/*===================================================================*/
/* 标准 NES 调色板，RGB555（取上游 InfoNES linux 层同一张表）。
 * PalTable 里存的就是这里的高 15 位，bit15 被 InfoNES 借去当
 * "背景色 0 = 透明" 的标记，所以取值时要 & 0x7FFF。 */
WORD NesPalette[ 64 ] =
{
  0x39ce, 0x1071, 0x0015, 0x2013, 0x440e, 0x5402, 0x5000, 0x3c20,
  0x20a0, 0x0100, 0x0140, 0x00e2, 0x0ceb, 0x0000, 0x0000, 0x0000,
  0x5ef7, 0x01dd, 0x10fd, 0x401e, 0x5c17, 0x700b, 0x6ca0, 0x6521,
  0x45c0, 0x0240, 0x02a0, 0x0247, 0x0211, 0x0000, 0x0000, 0x0000,
  0x7fff, 0x1eff, 0x2e5f, 0x223f, 0x79ff, 0x7dd6, 0x7dcc, 0x7e67,
  0x7ae7, 0x4342, 0x2769, 0x2ff3, 0x03bb, 0x0000, 0x0000, 0x0000,
  0x7fff, 0x579f, 0x635f, 0x6b3f, 0x7f1f, 0x7f1b, 0x7ef6, 0x7f75,
  0x7f94, 0x73f4, 0x57d7, 0x5bf9, 0x4ffe, 0x0000, 0x0000, 0x0000
};

/*===================================================================*/
/*  ROM                                                              */
/*===================================================================*/
static const pb_nes_rom_t *s_rom;

void pb_nes_set_rom( const pb_nes_rom_t *rom )
{
  s_rom = rom;
}

int InfoNES_ReadRom( const char *pszFileName )
{
/*
 * 读 ROM。Pico 上没有文件系统，这里直接把内嵌（flash 里）的那份交给核心。
 * ROM 只被核心读，不会被写，所以指针指向 flash 是安全的；需要可写 CHR 的
 * 游戏（byVRomSize == 0）走的是 PPURAM，不会写 ROM。
 */
  ( void )pszFileName;

  if ( s_rom == NULL || s_rom->data == NULL || s_rom->size < 16 )
    return -1;

  /* 先把 16 字节 iNES 头拷进核心 */
  InfoNES_MemoryCopy( &NesHeader, s_rom->data, sizeof( NesHeader ) );

  if ( memcmp( NesHeader.byID, "NES\x1a", 4 ) != 0 )
    return -1;

  uint32_t prg_size = ( uint32_t )NesHeader.byRomSize * 0x4000u;
  uint32_t chr_size = ( uint32_t )NesHeader.byVRomSize * 0x2000u;
  uint32_t offset   = 16u;

  /* Trainer 占 512 字节 */
  if ( NesHeader.byInfo1 & 4 )
    offset += 512u;

  if ( offset + prg_size + chr_size > s_rom->size )
    return -1;

  /* Clear SRAM */
  InfoNES_MemorySet( SRAM, 0, SRAM_SIZE );

  /* 如果带 trainer，塞到 $7000-$71FF */
  if ( NesHeader.byInfo1 & 4 )
    InfoNES_MemoryCopy( &SRAM[ 0x1000 ], s_rom->data + 16, 512 );

  ROM = ( BYTE * )( s_rom->data + offset );

  if ( NesHeader.byVRomSize > 0 )
    VROM = ROM + prg_size;
  else
    VROM = NULL;

  return 0;
}

void InfoNES_ReleaseRom()
{
  /* ROM 在 flash 里，没东西可释放 */
  ROM  = NULL;
  VROM = NULL;
}

/*===================================================================*/
/*  显示                                                             */
/*===================================================================*/
/* 一行 240 个 RGB565，交给 BSP 打包成像素对塞进带池 */
static uint16_t s_line565[ PB_NES_VIEW_W ];

void InfoNES_LoadLine( int nLine )
{
/*
 * 核心每渲染完一条可见扫描线就调这里。WorkFrame 里是 RGB555 + 优先级位，
 * 转成面板要的 RGB565 后立刻送屏。
 *
 * RGB555 -> RGB565：R/B 保持高位，绿从 5 位补到 6 位（把原来的最高位当新
 * 的最低位，等于最邻近展开），一条公式几拍就能算完。
 */
  const WORD *src = WorkFrame + PB_NES_CROP_X;

  for ( int x = 0; x < PB_NES_VIEW_W; ++x )
  {
    uint16_t v = ( uint16_t )( src[ x ] & 0x7FFF );

    s_line565[ x ] = ( uint16_t )(
        ( ( v & 0x7FE0 ) << 1 ) |    /* R(5) -> bit15..11, G(5) -> bit10..6 */
        ( ( v & 0x0200 ) >> 4 ) |    /* G 的第 5 位补到 bit5 */
        ( v & 0x001F ) );            /* B 保持 */
  }

  pb_push_nes_line( s_line565, ( uint8_t )nLine );
}

void InfoNES_LoadFrame()
{
  /* 240 行都送完了，把最后一条带交给 core1 */
  pb_display_nes_end_frame();
}

/*===================================================================*/
/*  输入                                                             */
/*===================================================================*/
void InfoNES_PadState( DWORD *pdwPad1, DWORD *pdwPad2, DWORD *pdwSystem )
{
/*
 * NES 手柄 1 位序（$4016 逐位读出）：A B Select Start Up Down Left Right
 * PicoBoy：A->A，B->B，Y->Select，X->Start
 */
  uint8_t k = pb_input_read();
  DWORD   p = 0;

  if ( k & PB_BTN_A )     p |= 1u << 0;
  if ( k & PB_BTN_B )     p |= 1u << 1;
  if ( k & PB_BTN_Y )     p |= 1u << 2;   /* Select */
  if ( k & PB_BTN_X )     p |= 1u << 3;   /* Start  */
  if ( k & PB_BTN_UP )    p |= 1u << 4;
  if ( k & PB_BTN_DOWN )  p |= 1u << 5;
  if ( k & PB_BTN_LEFT )  p |= 1u << 6;
  if ( k & PB_BTN_RIGHT ) p |= 1u << 7;

  *pdwPad1   = p;
  *pdwPad2   = 0;
  *pdwSystem = 0;
}

/*===================================================================*/
/*  内存                                                             */
/*===================================================================*/
void *InfoNES_MemoryCopy( void *dest, const void *src, int count )
{
  return memcpy( dest, src, ( size_t )count );
}

void *InfoNES_MemorySet( void *dest, int c, int count )
{
  return memset( dest, c, ( size_t )count );
}

/*===================================================================*/
/*  杂项                                                             */
/*===================================================================*/
int InfoNES_Menu()
{
  /* 菜单由 main.cpp 负责，核心不会再问 */
  return 0;
}

void InfoNES_DebugPrint( char *pszMsg )
{
  printf( "%s\n", pszMsg );
}

void InfoNES_Wait()
{
  /* 帧节拍在 main.cpp 的循环里统一做，这里不需要等 */
}

void InfoNES_MessageBox( char *pszMsg, ... )
{
  /* 屏幕上的提示由调用方决定；这里只往串口/USB 打一份日志 */
  va_list ap;
  va_start( ap, pszMsg );
  vprintf( pszMsg, ap );
  va_end( ap );
  printf( "\n" );
}

/*===================================================================*/
/*  音频                                                             */
/*===================================================================*/
/* pAPU 固定每帧 735 个样本（44100/60）；PWM 载波是 61035Hz，
 * 一个 16639us 的帧需要约 1015.6 个样本，取整 1016 个。
 * 误差 0.05%，听不出来，也不会累积漂移（每帧都重新对齐）。 */
#define APU_IN_SAMPLES   735
#define AUDIO_OUT_SAMPLES 1016

static int16_t s_pcm[ AUDIO_OUT_SAMPLES ];
static int32_t s_dc_x;      /* 去直流用的两个历史值 */
static int32_t s_dc_y;
static bool    s_sound_open;

void InfoNES_SoundInit( void )
{
  s_sound_open = false;
  s_dc_x = 0;
  s_dc_y = 0;
}

int InfoNES_SoundOpen( int samples_per_sync, int sample_rate )
{
  ( void )samples_per_sync;
  ( void )sample_rate;
  s_sound_open = true;
  s_dc_x = 0;
  s_dc_y = 0;
  return 0;
}

void InfoNES_SoundClose( void )
{
  s_sound_open = false;
}

void InfoNES_SoundOutput( int samples, BYTE *wave1, BYTE *wave2,
                          BYTE *wave3, BYTE *wave4, BYTE *wave5 )
{
/*
 * 把 pAPU 的 5 路波形（0..510 / 0..255 / ...）混成单声道，线性插值重采样到
 * 61035Hz，再去直流、限幅，丢给 BSP 的 DMA 环形缓冲。
 *
 * 注意 pAPU 的波形是"单极性"的（静音=0，发声时是正的上包络），直接播会有
 * 直流偏置，所以过一级一阶高通（截止 ~38Hz）把直流拉回 0。
 */
  if ( !s_sound_open || samples <= 0 )
    return;
  if ( samples > APU_IN_SAMPLES )
    samples = APU_IN_SAMPLES;

  uint32_t step = ( ( uint32_t )samples << 16 ) / AUDIO_OUT_SAMPLES;
  uint32_t pos  = 0;

  for ( int n = 0; n < AUDIO_OUT_SAMPLES; ++n )
  {
    uint32_t idx  = pos >> 16;
    int32_t  frac = ( int32_t )( pos & 0xFFFFu );

    if ( idx >= ( uint32_t )( samples - 1 ) )    /* 末尾两个样本并一起 */
    {
      idx  = ( uint32_t )( samples - 1 );
      frac = 0;
    }

    int32_t a = wave1[ idx ] + wave2[ idx ] + wave3[ idx ] +
                wave4[ idx ] + wave5[ idx ];
    int32_t b = wave1[ idx + 1 ] + wave2[ idx + 1 ] + wave3[ idx + 1 ] +
                wave4[ idx + 1 ] + wave5[ idx + 1 ];
    int32_t v = a + ( ( ( b - a ) * frac ) >> 16 );

    /* 一阶去直流：y[n] = x[n] - x[n-1] + 0.996 * y[n-1] */
    int32_t y = v - s_dc_x + ( ( s_dc_y * 255 ) >> 8 );
    s_dc_x = v;
    s_dc_y = y;

    int32_t out = y * 24;                        /* 满幅约 ±3 万 */
    if ( out >  32000 ) out =  32000;
    if ( out < -32000 ) out = -32000;

    s_pcm[ n ] = ( int16_t )out;

    pos += step;
  }

  pb_audio_submit( s_pcm, AUDIO_OUT_SAMPLES );
}
