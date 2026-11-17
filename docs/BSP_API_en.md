# PicoBoy BSP API Reference (English)

The PicoBoy BSP is the single board support package shared by both the GBC and NES
targets, built for RP2040 + ST7789. Design principles: **no external dependencies,
plain C-style interfaces, static arrays, no STL, and every pin/geometry parameter can
be overridden at compile time with `-D`.**

- Current version: `2.0` (see `PICOBOY_BSP_VERSION_*` in `board.h`)
- Headers: `bsp/include/picoboy/`
- Sources: `bsp/src/`
- Linking: add the `picoboy` static library to your target in CMake
  (`target_link_libraries(app PRIVATE picoboy)`)

---

## 1. Module Overview

| Header | Responsibility |
|---|---|
| `board.h` | Version, system clock, all pin and panel geometry constants |
| `pb_display.h` | ST7789 display: framebuffer, primitives, direct push, async block push, dual-core pusher, GB/NES line feed |
| `pb_audio.h` | Audio: one API shared by the PWM DAC and I2S backends |
| `pb_input.h` | Eight buttons: level state plus edge detection |
| `pb_settings.h` | Brightness/volume/invert settings: flash persistence and settings page |
| `pb_flash_rom.h` | Scans the ustar tar in the flash ROM area |
| `pb_rom_menu.h` | Multi-ROM selection menu decoupled from data structures |
| `font3x5.h` | Built-in 3x5 bitmap font and glyph accessor |

---

## 2. board.h — Board Constants

### Version macros
```c
PICOBOY_BSP_VERSION_MAJOR   // 2
PICOBOY_BSP_VERSION_MINOR   // 0
PICOBOY_BSP_VERSION_STRING  // "2.0"
```

### Main constants (all overridable with `-D`)
| Constant | Default | Meaning |
|---|---|---|
| `PB_SYS_CLOCK_KHZ` | 250000 | System clock (kHz) |
| `PB_PIN_LCD_SCK/MOSI/CS/DC/RST/BL` | 6/7/5/9/4/12 | Display SPI and control pins; use -1 when RST/BL is absent |
| `PB_LCD_SPI_HZ` | 62500000 | Pixel-phase SPI bit clock (ST7789 safe limit) |
| `PB_LCD_CMD_HZ` | 8000000 | Command-phase bit clock |
| `PB_SCREEN_MODE` | 0 | Physical panel: 0=240x240, 1=240x320 |
| `PB_PANEL_W/H` | Derived from mode | Physical GRAM size |
| `PB_LCD_ROTATE` | 0 | 0=portrait, 1=landscape 90° |
| `PB_LCD_MADCTL` | 0x00 / 0x60 | Panel direction register |
| `PB_LCD_X/Y_OFFSET` | 0 | GRAM origin offset |
| `PB_LCD_INVERT` | 1 | Inversion: 1=send 0x21, 0=send 0x20 |
| `PB_DISPLAY_SCALE15` | 1 | 1.5x nearest-neighbor GB scaling (requires dual core) |
| `PB_SCREEN_W/H` | Derived from rotation | Logical panel size (width/height swap when rotated) |
| `PB_FB_W/H/X/Y` | See header | Framebuffer geometry; landscape FB is 256x240 centered |
| `PB_PIN_AUDIO` | 11 | PWM audio pin |
| `PB_AUDIO_PWM_BITS` | 12 | PWM resolution |
| `PB_AUDIO_RING` | 2048 | Audio ring buffer sample count (power of two) |
| `PB_AUDIO_SAMPLE_RATE` | 61035 | Sample rate (= PWM carrier) |
| `PB_AUDIO_I2S` | 0 | 0=PWM, 1=PIO I2S |
| `PB_PIN_I2S_DATA/BCLK/LRCLK` | 13/14/15 | I2S data and clock pins (BCLK/LRCLK adjacent) |
| `PB_PIN_BTN_*` | See header | A/B/X/Y/D-pad GPIOs, active high |

---

## 3. pb_display.h — Display

### Framebuffer and initialization
```c
void     pb_display_init(void);
extern uint16_t pb_fb[PB_FB_W * PB_FB_H];   // Read/writable full framebuffer
```

### Color and panel settings
```c
uint16_t PB_RGB(r,g,b);                     // 8-bit RGB to RGB565
// Common colors: PB_BLACK / PB_WHITE / PB_RED / PB_GREEN / PB_BLUE / PB_CYAN / PB_MAGENTA / PB_YELLOW
void pb_display_set_invert(bool on);        // Panel inversion
void pb_display_set_gb_palette(const uint16_t pal4[4]);  // Four-level GB palette
void pb_display_set_madctl(uint8_t madctl); // Change MADCTL
```

### Single-core framebuffer primitives (flush after drawing)
```c
void pb_clear(uint16_t color);
void pb_set_pixel(int x, int y, uint16_t color);
void pb_fill_rect(int x, int y, int w, int h, uint16_t color);
void pb_text(int x, int y, const char *s, uint16_t fg, uint16_t bg);
void pb_text_scaled(int x, int y, const char *s, uint8_t scale, uint16_t fg, uint16_t bg);
int  pb_text_width(const char *s, uint8_t scale);
void pb_text_center(int y, const char *s, uint8_t scale, uint16_t fg, uint16_t bg);
void pb_backlight(uint8_t level);           // Backlight 0..255
```

### Pushing to the panel
```c
void pb_display_flush(void);                // Full-frame DMA push (blocks until done)
void pb_display_flush_region(int x, int y, int w, int h);  // Partial refresh
```

### Direct panel writes (bypassing the framebuffer)
```c
void pb_fill_screen(uint16_t color);        // Fill the whole panel
void pb_display_push_rect(int x, int y, int w, int h,
                          const uint16_t *rgb565);  // Direct rect push; even width; blocking

// Async block push (for emulator double-buffered output): start opens the window,
// words rearms DMA and returns immediately, flush waits for completion and closes.
// The caller prepares the next block while DMA moves the current one.
void pb_display_push_start(int x, int y, int w, int h);
void pb_display_push_words(const uint32_t *words, size_t nwords);
void pb_display_push_flush(void);
```

### Dual-core streaming (core0 fills bands, core1 pushes)
```c
void pb_display_start_pusher(void);         // Start the core1 pusher worker
void pb_display_wait_idle(void);            // Wait until all delivered bands are pushed
void pb_display_begin_frame(void);          // GB frame begin
void pb_display_end_frame(void);            // GB frame end
void pb_push_gb_line(const uint8_t *pixels, uint8_t line);   // 160 pixels, 2-bit color index
void pb_push_rgb_line(const uint16_t *colors, uint8_t line); // 160 RGB565 pixels (GBC true color)
void pb_display_nes_begin_frame(void);
void pb_display_nes_end_frame(void);
void pb_push_nes_line(const uint16_t *colors, uint8_t line); // 240 pixels, 1:1
```

---

## 4. pb_audio.h — Audio

The PWM and I2S backends share one API; the active backend is selected by the build
option `PB_AUDIO_I2S`.

```c
void pb_audio_init(void);                   // Init + DMA ring buffer (silent by default)
void pb_audio_set_volume(uint8_t vol);      // Volume 0..10 (default 8)

// Non-blocking streaming
void pb_audio_submit(const int16_t *mono, size_t count);  // Write to ring; drops when full
size_t pb_audio_free(void);                 // Samples the ring can still accept (0=full); flow control to avoid crackle

// Sustained tone (non-blocking; hardware-generated, no CPU cost)
void pb_tone_start(uint32_t freq_hz);       // freq=0 is equivalent to stop
void pb_tone_stop(void);

// Blocking interfaces
void pb_beep(uint32_t freq_hz, uint32_t duration_ms);
void pb_audio_play_sample(const int16_t *samples, size_t count, uint32_t sample_rate);
```

---

## 5. pb_input.h — Input

### Button bit masks
```c
PB_BTN_A 0x01   PB_BTN_B 0x02   PB_BTN_Y 0x04   PB_BTN_X 0x08
PB_BTN_RIGHT 0x10  PB_BTN_LEFT 0x20  PB_BTN_UP 0x40  PB_BTN_DOWN 0x80
```

### Interface
```c
void   pb_input_init(void);                 // Initialize eight GPIOs (internal pull-down)
uint8_t pb_input_read(void);                // Current button bit mask
void   pb_input_poll(void);                 // Call once per frame to refresh edges
int    pb_input_held(uint8_t btn);          // Whether held
int    pb_input_just_pressed(uint8_t btn);  // Pressed this frame
int    pb_input_just_released(uint8_t btn); // Released this frame
```

---

## 6. pb_settings.h — Persistent Settings

### Ranges and defaults
```c
PB_SETTINGS_BRIGHT_MIN 1 / MAX 10 / DEFAULT 9
PB_SETTINGS_VOL_MIN 0 / MAX 10 / DEFAULT 8
PB_SETTINGS_FLASH_OFFSET   // Default last sector (PICO_FLASH_SIZE_BYTES - 4096)

typedef struct { uint8_t brightness; uint8_t volume; uint8_t invert; } pb_settings_t;
```

### Interface
```c
void pb_settings_init(void);                // Read flash and apply; defaults if absent (no write)
void pb_settings_load(void);                // Read into memory only; do not touch hardware
void pb_settings_apply(void);               // Apply to backlight/volume
int  pb_settings_save(void);                // Save: 1=written, 0=skipped unchanged, -1=no valid area
const pb_settings_t *pb_settings_get(void);

void pb_settings_set_brightness(uint8_t level);  // Auto-clamped and applied immediately
void pb_settings_set_volume(uint8_t level);
void pb_settings_set_invert(uint8_t on);

int  pb_settings_menu(void);                // Full-screen settings page, blocks until return; 1=changed
```

> **Constraint**: XIP halts while `pb_settings_save()` erases/flash-writes. It must only
> be called **before the core1 pusher worker is started**.

---

## 7. pb_flash_rom.h — Flash ROM Scanning

```c
#define PB_ROM_AREA_ADDR 0x10120000u        // ROM area base (overridable with -D)

typedef struct {
    const char    *name;                    // File name inside the tar
    const uint8_t *data;                    // ROM data start (XIP; no RAM copy)
    uint32_t       size;
} pb_flash_rom_entry_t;

typedef bool (*pb_rom_validator_fn)(const uint8_t *data, uint32_t size);

bool pb_flash_rom_is_tar(void);             // Check the first block's ustar magic
int  pb_flash_rom_scan(pb_flash_rom_entry_t *out, int cap,
                       pb_rom_validator_fn validator);  // Returns entry count; 0 if none
```

---

## 8. pb_rom_menu.h — Selection Menu

```c
typedef struct { const char *name; const char *info; } pb_menu_item_t;
typedef struct {
    const char *title; const char *subtitle; const char *hint;
    uint16_t info_color;
} pb_menu_style_t;

void pb_rom_menu_set_setup_cb(void (*cb)(void));       // Y-key callback; NULL=disabled
int  pb_rom_menu_select(const pb_menu_style_t *style,
                        const pb_menu_item_t *items, int count);  // Returns index; -1 if zero items
void pb_rom_menu_render(const pb_menu_style_t *style,
                        const pb_menu_item_t *items, int count, int sel, int top); // Draw one frame only
void pb_rom_menu_splash(const char *name);             // Transition into the game
```

---

## 9. font3x5.h — Bitmap Font

```c
const uint8_t *font3x5_glyph(char c);
// Covers 0-9 / A-Z (lowercase mapped to uppercase) / . : - / _ ? ! + space;
// unsupported characters return the space glyph.
```

---

## 10. Recommended Initialization Order (single-core menu phase)

```c
pb_display_init();
pb_audio_init();
pb_input_init();
pb_settings_init();                 // = load + apply

// Start the dual-core pusher before entering the game
pb_display_start_pusher();
// GBC: begin_frame / push_rgb_line / end_frame
// NES: nes_begin_frame / push_nes_line / nes_end_frame
```

### Threading and safety constraints
- Framebuffer primitives and text are used only in the single-core menu phase. Once the
  dual-core pusher starts, `pb_fb` is reused as a band pool; the two are mutually exclusive.
- Flash writes (settings save) must complete before the core1 pusher starts.
- Audio `submit` is non-blocking and drops samples when the buffer is full; pair it with
  `pb_audio_free()` for flow control to avoid the silent drops that cause crackle.
- Async block push does not conflict with the core1 pusher (the latter is only started
  by `pb_display_start_pusher()`).
