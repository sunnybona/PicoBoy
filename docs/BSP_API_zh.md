# PicoBoy BSP API 说明（中文）

PicoBoy BSP 是 GBC 与 NES 两个目标共用的唯一板级支持包，面向 RP2040 + ST7789。
设计原则：**零外部依赖、纯 C 风格接口、静态数组、不用 STL、可在编译期用 `-D` 覆盖所有引脚与几何参数。**

- 当前版本：`2.0`（见 `board.h` 中 `PICOBOY_BSP_VERSION_*`）
- 头文件目录：`bsp/include/picoboy/`
- 源码目录：`bsp/src/`
- 链接方式：CMake 中把 `picoboy` 静态库链到目标即可（`target_link_libraries(app PRIVATE picoboy)`）

---

## 1. 模块总览

| 头文件 | 职责 |
|---|---|
| `board.h` | 版本、系统主频、全部引脚与面板几何常量 |
| `pb_display.h` | ST7789 显示：帧缓冲、图元、直推、异步块推送、双核推屏、GB/NES 喂线 |
| `pb_audio.h` | 音频：PWM DAC 与 I2S 共用同一套 API |
| `pb_input.h` | 8 个按键：电平稳态 + 边沿判断 |
| `pb_settings.h` | 亮度/音量/反色设置：flash 持久化与设置页 |
| `pb_flash_rom.h` | 扫描 flash ROM 区的 ustar tar |
| `pb_rom_menu.h` | 与数据结构解耦的多 ROM 选择菜单 |
| `font3x5.h` | 内置 3x5 点阵字形与取字函数 |

---

## 2. board.h — 板级常量

### 版本宏
```c
PICOBOY_BSP_VERSION_MAJOR   // 2
PICOBOY_BSP_VERSION_MINOR   // 0
PICOBOY_BSP_VERSION_STRING  // "2.0"
```

### 主要常量（均可 `-D` 覆盖）
| 常量 | 默认值 | 含义 |
|---|---|---|
| `PB_SYS_CLOCK_KHZ` | 250000 | 系统主频（kHz） |
| `PB_PIN_LCD_SCK/MOSI/CS/DC/RST/BL` | 6/7/5/9/4/12 | 屏幕 SPI 与控制脚，RST/BL 无则设 -1 |
| `PB_LCD_SPI_HZ` | 62500000 | 像素相位 SPI 位时钟（ST7789 安全上限） |
| `PB_LCD_CMD_HZ` | 8000000 | 命令相位位时钟 |
| `PB_SCREEN_MODE` | 0 | 物理面板：0=240x240，1=240x320 |
| `PB_PANEL_W/H` | 由模式推导 | 物理 GRAM 尺寸 |
| `PB_LCD_ROTATE` | 0 | 0=竖屏，1=横屏 90° |
| `PB_LCD_MADCTL` | 0x00 / 0x60 | 面板方向寄存器 |
| `PB_LCD_X/Y_OFFSET` | 0 | GRAM 原点偏移 |
| `PB_LCD_INVERT` | 1 | 反色：1=发 0x21，0=发 0x20 |
| `PB_DISPLAY_SCALE15` | 1 | GB 画面 1.5 倍最近邻（需双核） |
| `PB_SCREEN_W/H` | 由旋转推导 | 逻辑面板尺寸（旋转时宽高交换） |
| `PB_FB_W/H/X/Y` | 见头文件 | 帧缓冲几何；横屏 FB 取 256x240 居中 |
| `PB_PIN_AUDIO` | 11 | PWM 音频脚 |
| `PB_AUDIO_PWM_BITS` | 12 | PWM 分辨率 |
| `PB_AUDIO_RING` | 2048 | 音频环形缓冲样本数（2 的幂） |
| `PB_AUDIO_SAMPLE_RATE` | 61035 | 采样率（= PWM 载波） |
| `PB_AUDIO_I2S` | 0 | 0=PWM，1=PIO I2S |
| `PB_PIN_I2S_DATA/BCLK/LRCLK` | 13/14/15 | I2S 数据与时钟脚（BCLK/LRCLK 相邻） |
| `PB_PIN_BTN_*` | 见头文件 | A/B/X/Y/方向键 GPIO，高电平有效 |

---

## 3. pb_display.h — 显示

### 帧缓冲与初始化
```c
void     pb_display_init(void);
extern uint16_t pb_fb[PB_FB_W * PB_FB_H];   // 可直接读写的整帧缓冲
```

### 颜色与面板设置
```c
uint16_t PB_RGB(r,g,b);                     // 8 位 RGB 转 RGB565
// 常用色：PB_BLACK / PB_WHITE / PB_RED / PB_GREEN / PB_BLUE / PB_CYAN / PB_MAGENTA / PB_YELLOW
void pb_display_set_invert(bool on);        // 面板反色
void pb_display_set_gb_palette(const uint16_t pal4[4]);  // GB 四档调色板
void pb_display_set_madctl(uint8_t madctl); // 改 MADCTL
```

### 单核帧缓冲图元（画完需 flush）
```c
void pb_clear(uint16_t color);
void pb_set_pixel(int x, int y, uint16_t color);
void pb_fill_rect(int x, int y, int w, int h, uint16_t color);
void pb_text(int x, int y, const char *s, uint16_t fg, uint16_t bg);
void pb_text_scaled(int x, int y, const char *s, uint8_t scale, uint16_t fg, uint16_t bg);
int  pb_text_width(const char *s, uint8_t scale);
void pb_text_center(int y, const char *s, uint8_t scale, uint16_t fg, uint16_t bg);
void pb_backlight(uint8_t level);           // 背光 0..255
```

### 上屏
```c
void pb_display_flush(void);                // 整帧 DMA 推屏（阻塞到推完）
void pb_display_flush_region(int x, int y, int w, int h);  // 局部刷新
```

### 直接写屏（不经帧缓冲）
```c
void pb_fill_screen(uint16_t color);        // 整面板填色
void pb_display_push_rect(int x, int y, int w, int h,
                          const uint16_t *rgb565);  // 任意矩形直推，宽度须偶数，阻塞

// 异步块推送（模拟器双缓冲推屏用）：start 开窗启动，words 挂 DMA 后立即返回，
// flush 等传输完并收尾。DMA 搬一块像素时调用方可同时准备下一块。
void pb_display_push_start(int x, int y, int w, int h);
void pb_display_push_words(const uint32_t *words, size_t nwords);
void pb_display_push_flush(void);
```

### 双核流式（core0 填带，core1 推屏）
```c
void pb_display_start_pusher(void);         // 启动 core1 推屏 worker
void pb_display_wait_idle(void);            // 等已交付带全部推完
void pb_display_begin_frame(void);          // GB 帧开始
void pb_display_end_frame(void);            // GB 帧结束
void pb_push_gb_line(const uint8_t *pixels, uint8_t line);   // 160 像素 2 位色号
void pb_push_rgb_line(const uint16_t *colors, uint8_t line); // 160 像素 RGB565（GBC 真彩色）
void pb_display_nes_begin_frame(void);
void pb_display_nes_end_frame(void);
void pb_push_nes_line(const uint16_t *colors, uint8_t line); // 240 像素 1:1
```

---

## 4. pb_audio.h — 音频

PWM 与 I2S 两个后端共用同一套 API，由编译选项 `PB_AUDIO_I2S` 选择。

```c
void pb_audio_init(void);                   // 初始化 + DMA 环形缓冲（默认静音）
void pb_audio_set_volume(uint8_t vol);      // 音量 0..10（默认 8）

// 非阻塞流式
void pb_audio_submit(const int16_t *mono, size_t count);  // 写环形缓冲，满则丢弃
size_t pb_audio_free(void);                 // 环形缓冲还能装多少样本（0=满），用于上游流控避免破音

// 持续音调（非阻塞，硬件发声不占 CPU）
void pb_tone_start(uint32_t freq_hz);       // freq=0 等同停止
void pb_tone_stop(void);

// 阻塞接口
void pb_beep(uint32_t freq_hz, uint32_t duration_ms);
void pb_audio_play_sample(const int16_t *samples, size_t count, uint32_t sample_rate);
```

---

## 5. pb_input.h — 按键

### 按键位掩码
```c
PB_BTN_A 0x01   PB_BTN_B 0x02   PB_BTN_Y 0x04   PB_BTN_X 0x08
PB_BTN_RIGHT 0x10  PB_BTN_LEFT 0x20  PB_BTN_UP 0x40  PB_BTN_DOWN 0x80
```

### 接口
```c
void   pb_input_init(void);                 // 初始化 8 个 GPIO（内部下拉）
uint8_t pb_input_read(void);                // 当前按键位掩码
void   pb_input_poll(void);                 // 每帧调一次，刷新边沿
int    pb_input_held(uint8_t btn);          // 是否按住
int    pb_input_just_pressed(uint8_t btn);  // 本帧刚按下
int    pb_input_just_released(uint8_t btn); // 本帧刚松开
```

---

## 6. pb_settings.h — 设置持久化

### 范围与默认
```c
PB_SETTINGS_BRIGHT_MIN 1 / MAX 10 / DEFAULT 9
PB_SETTINGS_VOL_MIN 0 / MAX 10 / DEFAULT 8
PB_SETTINGS_FLASH_OFFSET   // 默认最后一扇区（PICO_FLASH_SIZE_BYTES - 4096）

typedef struct { uint8_t brightness; uint8_t volume; uint8_t invert; } pb_settings_t;
```

### 接口
```c
void pb_settings_init(void);                // 读 flash 并立即应用；无数据用默认（不写）
void pb_settings_load(void);                // 只读入内存，不碰硬件
void pb_settings_apply(void);               // 应用到背光/音量
int  pb_settings_save(void);                // 存 flash：1=已写，0=无变化跳过，-1=无有效区
const pb_settings_t *pb_settings_get(void);

void pb_settings_set_brightness(uint8_t level);  // 自动钳位并立即生效
void pb_settings_set_volume(uint8_t level);
void pb_settings_set_invert(uint8_t on);

int  pb_settings_menu(void);                // 全屏设置页，阻塞到返回；1=有改动
```

> **限制**：`pb_settings_save()` 擦写 flash 期间 XIP 停止，只能在 **core1 推屏 worker 启动之前**调用。

---

## 7. pb_flash_rom.h — flash ROM 扫描

```c
#define PB_ROM_AREA_ADDR 0x10120000u        // ROM 区基址（可 -D 覆盖）

typedef struct {
    const char    *name;                    // tar 内文件名
    const uint8_t *data;                    // ROM 数据起点（XIP，不做 RAM 拷贝）
    uint32_t       size;
} pb_flash_rom_entry_t;

typedef bool (*pb_rom_validator_fn)(const uint8_t *data, uint32_t size);

bool pb_flash_rom_is_tar(void);             // 首块 ustar 魔数判断
int  pb_flash_rom_scan(pb_flash_rom_entry_t *out, int cap,
                       pb_rom_validator_fn validator);  // 返回条目数，0=无
```

---

## 8. pb_rom_menu.h — 选择菜单

```c
typedef struct { const char *name; const char *info; } pb_menu_item_t;
typedef struct {
    const char *title; const char *subtitle; const char *hint;
    uint16_t info_color;
} pb_menu_style_t;

void pb_rom_menu_set_setup_cb(void (*cb)(void));       // Y 键回调，NULL=禁用
int  pb_rom_menu_select(const pb_menu_style_t *style,
                        const pb_menu_item_t *items, int count);  // 返回选中下标；0 项返回 -1
void pb_rom_menu_render(const pb_menu_style_t *style,
                        const pb_menu_item_t *items, int count, int sel, int top); // 仅画一帧
void pb_rom_menu_splash(const char *name);             // 进游戏过场
```

---

## 9. font3x5.h — 点阵字形

```c
const uint8_t *font3x5_glyph(char c);
// 覆盖 0-9 / A-Z（小写按大写）/ . : - / _ ? ! + 空格；不支持的字符返回空格字形
```

---

## 10. 推荐初始化顺序（单核菜单阶段）

```c
pb_display_init();
pb_audio_init();
pb_input_init();
pb_settings_init();                 // = load + apply

// 进游戏前再启动双核推屏
pb_display_start_pusher();
// GBC: begin_frame / push_rgb_line / end_frame
// NES: nes_begin_frame / push_nes_line / nes_end_frame
```

### 线程与安全约束
- 帧缓冲图元与文本只在单核菜单阶段使用；启动双核推屏后，`pb_fb` 被当作带池复用，二者互斥。
- flash 擦写（设置保存）必须在 core1 推屏启动之前完成。
- 音频 `submit` 为非阻塞、缓冲满直接丢弃；配合 `pb_audio_free()` 做流控可避免静默丢样本造成破音。
- 异步块推送与 core1 推屏互不冲突（后者仅由 `pb_display_start_pusher()` 启动）。
