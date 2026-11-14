/*
 * menu_preview.cpp — 在 PC 上把 ROM 菜单渲染成图片，检查两个工程是否真的同一套外观
 *
 * 直接编译 BSP 里那份 pb_rom_menu.cpp（GB 和 NES 共用的同一份），用最小桩
 * 实现代替 PIO/DMA 版显示，输出 PPM。传入 gbc / nes 两套真实条目数据，
 * 渲染出来的两张图除文案外应当逐像素一致 —— 这就是「风格完全统一」的验收。
 *
 * 构建（在 E:\PicoBoy 下，mingw g++）：
 *   g++ -O2 -DPB_ROM_MENU_HOST -I bsp/include \
 *       tools/menu_preview.cpp bsp/src/pb_rom_menu.cpp -o build/menu_preview.exe
 * 运行：
 *   build/menu_preview.exe build/menu_gbc.ppm gbc 2
 *   build/menu_preview.exe build/menu_nes.ppm nes 2
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "picoboy/pb_display.h"
#include "picoboy/pb_input.h"
#include "picoboy/pb_audio.h"
#include "picoboy/pb_rom_menu.h"
#include "picoboy/font3x5.h"

extern "C" {

/* ---- 帧缓冲桩（与 bsp/src/pb_display.cpp 行为一致） ---- */
uint16_t pb_fb[PB_SCREEN_W * PB_SCREEN_H];

void pb_clear(uint16_t color)
{
    for(int i = 0; i < PB_SCREEN_W * PB_SCREEN_H; i++) pb_fb[i] = color;
}
void pb_set_pixel(int x, int y, uint16_t color)
{
    if(x < 0 || y < 0 || x >= PB_SCREEN_W || y >= PB_SCREEN_H) return;
    pb_fb[y * PB_SCREEN_W + x] = color;
}
void pb_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > PB_SCREEN_W ? PB_SCREEN_W : x + w;
    int y1 = y + h > PB_SCREEN_H ? PB_SCREEN_H : y + h;
    for(int yy = y0; yy < y1; yy++)
        for(int xx = x0; xx < x1; xx++)
            pb_fb[yy * PB_SCREEN_W + xx] = color;
}
/* 文本桩：与 pb_display.cpp 的 pb_text_scaled 同算法（5x3 字形逐点放大） */
void pb_text_scaled(int x, int y, const char *s, uint8_t scale,
                    uint16_t fg, uint16_t bg)
{
    if(scale < 1) scale = 1;
    int gx = 0;
    for(int n = 0; s[n]; n++)
    {
        const uint8_t *g = font3x5_glyph(s[n]);
        for(int r = 0; r < 5; r++)
            for(int c = 0; c < 3; c++)
            {
                uint16_t col = ((g[r] >> (2 - c)) & 1) ? fg : bg;
                pb_fill_rect(x + gx + c * scale, y + r * scale, scale, scale, col);
            }
        gx += 4 * scale;
    }
}
void pb_text(int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    pb_text_scaled(x, y, s, 1, fg, bg);
}
int pb_text_width(const char *s, uint8_t scale)
{
    if(scale < 1) scale = 1;
    int n = 0;
    while(s[n]) n++;
    return n ? n * 4 * (int)scale - (int)scale : 0;
}
void pb_text_center(int y, const char *s, uint8_t scale, uint16_t fg, uint16_t bg)
{
    pb_text_scaled((PB_SCREEN_W - pb_text_width(s, scale)) / 2, y, s, scale, fg, bg);
}
void pb_display_flush(void) { }
void pb_input_poll(void) { }
int pb_input_held(uint8_t btn) { (void)btn; return 0; }
int pb_input_just_pressed(uint8_t btn) { (void)btn; return 0; }
uint8_t pb_input_read(void) { return 0; }
void pb_beep(uint32_t f, uint32_t ms) { (void)f; (void)ms; }

}   /* extern "C" */

/* ---------------- 两套真实条目数据 ---------------- */

/* GB：名字与 gbc/rom.h 里的 pb_roms[] 一致，信息行按 header 换算 */
static const pb_menu_item_t k_gbc[] = {
    { "zelda dx",            "1024K  MBC1  CGB" },
    { "pocket bomberman",    "1024K  MBC5  CGB" },
    { "Spider-Man 2 - The Sinister Six (USA, Eu", "1024K  MBC5  CGB" },
    { "dd3 arcade",          "128K  MBC1  DMG"  },
    { "double dragon",       "128K  MBC1  DMG"  },
    { "mario yoshi",         "64K  NO MBC  DMG" },
    { "spiderman2",          "128K  MBC1  DMG"  },
};

/* NES：与 nes_rom.h 里 9 份 ROM 的实测参数一致 */
static const pb_menu_item_t k_nes[] = {
    { "CONTRA",      "MAPPER 23  PRG 128K  CHR 128K" },
    { "DRAGON",      "MAPPER 88  PRG 128K  CHR 128K" },
    { "GIJOE",       "MAPPER 4  PRG 128K  CHR 256K"  },
    { "GUEVARA",     "MAPPER 1  PRG 128K  CHR 128K"  },
    { "JACKAL",      "MAPPER 2  PRG 128K  CHR RAM"   },
    { "LIFEFORCE",   "MAPPER 73  PRG 128K  CHR RAM"  },
    { "RUSHNA",      "MAPPER 2  PRG 128K  CHR RAM"   },
    { "SAIYUKI2",    "MAPPER 18  PRG 128K  CHR 128K" },
    { "PB NES TEST", "MAPPER 0  PRG 16K  CHR 8K"     },
};

static void write_ppm(const char *path)
{
    FILE *o = fopen(path, "wb");
    if(!o) { printf("write failed: %s\n", path); return; }
    fprintf(o, "P6\n%d %d\n255\n", PB_SCREEN_W, PB_SCREEN_H);
    for(int i = 0; i < PB_SCREEN_W * PB_SCREEN_H; i++)
    {
        uint16_t c = pb_fb[i];
        unsigned r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
        unsigned char px[3] = {
            (unsigned char)((r << 3) | (r >> 2)),
            (unsigned char)((g << 2) | (g >> 4)),
            (unsigned char)((b << 3) | (b >> 2)),
        };
        fwrite(px, 1, 3, o);
    }
    fclose(o);
}

int main(int argc, char **argv)
{
    if(argc < 4)
    {
        printf("usage: menu_preview out.ppm {gbc|nes} sel\n");
        return 1;
    }

    int  sel  = atoi(argv[3]);
    bool isGbc = strcmp(argv[2], "gbc") == 0;

    const pb_menu_item_t *items = isGbc ? k_gbc : k_nes;
    int count = isGbc ? (int)(sizeof k_gbc / sizeof k_gbc[0])
                      : (int)(sizeof k_nes / sizeof k_nes[0]);

    char sub[32];
    snprintf(sub, sizeof sub, "%d ROMS  %s", count,
             isGbc ? "WALNUT-CGB" : "INFONES");

    pb_menu_style_t style;
    style.title      = isGbc ? "PICOBOY GBC" : "PICOBOY NES";
    style.subtitle   = sub;
    style.hint       = "A/X PLAY   Y SETUP";
    style.info_color = PB_CYAN;

    pb_rom_menu_render(&style, items, count, sel, 0);
    write_ppm(argv[1]);

    printf("wrote %s (%s, sel=%d, %d items)\n",
           argv[1], isGbc ? "gbc" : "nes", sel, count);
    return 0;
}
