# PicoBoy GBC — Walnut-CGB + PicoBoy BSP 掌机模拟器（多 ROM 版）

> **注意：本工程已并入 PicoBoy 整合工程**（`E:\PicoBoy`），与本目录配套的
> NES 工程在 `../nes`，板级代码只有共用的一份 `../bsp`。
> 构建入口在上级目录：`cmake -S .. -B ../build`，产出 `picoboy_gbc.uf2`。
> 另外 **ROM 早已改成 `roms/` 目录 + configure 时打包**（`tools/gen_gb_rom.py`，
> 生成物落到 `build/gb_rom.h`），本文正文里提到的 `rom.h` / `gen_rom.py` 是旧做法，
> 已随整合删除，结构说明以 `../README.md` 为准。

在 RP2040 + ST7789（240×240）掌上设备上运行的 **Game Boy / Game Boy Color**
模拟器，带 **多 ROM 选择菜单**。模拟器核心是
[Walnut-CGB](https://github.com/Mr-PauI/Walnut-CGB)（Peanut-GB 的性能重写版，
完整 CGB 支持 + 双取指 dispatch），硬件全部走 **PicoBoy BSP**
（显示 / 音频 / 按键统一抽象），声音用 `minigb_apu`。

和花生（Peanut-GB）DMG 版那套 BSP 完全共用，区别只在「每条扫描线怎么上色」：
DMG 走四档绿调色牌，CGB 直接查 Walnut 的 `gb->cgb.fixPalette[]` 拿 RGB565 真实色。

## 目录结构

```
picoboy_gbc/
├── CMakeLists.txt          # 顶层构建（BSP + minigb_apu + 模拟器 + 菜单）
├── pico_sdk_import.cmake
├── memmap_pico16m.ld       # SDK memmap 副本，FLASH 改成 16384k（本机 16MB flash）
├── main.cpp                # 前端：ROM 回调、逐行上色、按键、帧节奏
├── rom_menu.cpp / rom_menu.h # 多 ROM 选择菜单（C++17；调用方也是 C++，不需 extern "C"）
├── pb_roms.h               # 内嵌 ROM 表的数据类型（菜单不必解析几 MB 的 rom.h）
├── rom.h                   # 内嵌 ROM（由 gen_rom.py 生成，现在是 7 个）
├── gen_rom.py              # .gb/.gbc -> rom.h（多 ROM 自动生成描述表）
├── picoboy_bsp/            # PicoBoy BSP（复制自花生工程，已回写新接口，见「BSP 回写」）
│   ├── include/picoboy/    # board.h / pb_display.h / pb_audio.h / pb_input.h
│   └── src/                # pb_display.cpp / pb_audio.cpp / pb_input.cpp + .pio
├── minigb_apu/             # APU（取自 Walnut-CGB 的 SDL 示例）
├── third_party/Walnut-CGB/ # walnut_cgb.h 单头文件核心（git clone 得到）
└── tools/
    ├── host_smoke.cpp      # PC 上跑核心的冒烟测试（不需要硬件）
    └── menu_preview.cpp    # PC 上把菜单渲染成图片，做布局自检
```

## 内嵌的 7 个 ROM

| # | 菜单名 | 文件 | 容量 | MBC | 机型 |
|---|---|---|---|---|---|
| 1 | ZELDA DX | `zelda_dx.gbc` | 1024K | MBC5 | CGB |
| 2 | POCKET BOMBERMAN | `pocket_bomberman.gbc` | 1024K | MBC5 | CGB |
| 3 | SPIDER-MAN 2 - THE SINISTER SIX | `Spider-Man 2 …(USA, Europe).gbc` | 1024K | MBC5 | CGB |
| 4 | DD3 ARCADE | `dd3_arcade.gb` | 128K | MBC1 | DMG |
| 5 | DOUBLE DRAGON | `double_dragon.gb` | 128K | MBC1 | DMG |
| 6 | MARIO YOSHI | `mario_yoshi.gb` | 64K | MBC1 | DMG |
| 7 | SPIDERMAN2 | `spiderman2.gb` | 128K | MBC1 | DMG |

合计 3,604,480 字节（约 3.44MB）。桌面那份 `Double Dragon (USA, Europe).gb`
与 rom 目录里的 `double_dragon.gb` SHA256 完全一致，是重复文件，未重复收录；
`.nes`（BladeBuster、绿色兵团、魂斗罗、Ikari、Mighty Final Fight）是 FC 游戏，
本模拟器不支持。

## ROM 选择菜单

菜单只做一件事：把 `pb_roms[]` 画出来让用户挑，选完返回下标给 `main.cpp`
去 `gb_init()`。写法完全照 `picoboy_bsp/examples/demo.cpp`——整帧缓冲绘制 +
`pb_display_flush()` 上屏 + `pb_input_poll()` 取边沿。

- **布局**：顶部标题 `PICOBOY GBC` + `7 ROMS WALNUT-CGB`；中间一屏 7 行列表
  （序号 + 游戏名，选中行绿底 + 左侧黄条）；底部一行显示选中项的
  `容量 / MBC 类型 / 机型`（CGB 用青色、DMG 用绿色）；最下面是操作提示。
- **按键**：↑/↓ 移动（按下立刻走一格，按住 0.4s 后以 90ms 连发）；
  **A 或 X（Start）确认**；移动/确认各有一次 `pb_beep()` 短音。
- **字模**：菜单用 BSP 的 `pb_text_scaled()`（2 倍，每个点画成 2×2）和
  `pb_text_center()`（整屏居中），信息行用 1 倍。标题下划线宽度由
  `pb_text_width()` 算出来。这三个接口是本次做 GBC 时新加进 BSP 的，
  原来 `rom_menu.cpp` 里那份本地实现已经删掉。
- **长名字**：字模没有括号和逗号，所以显示名会砍掉 `(USA, Europe)` 这类区域
  后缀，超过 24 字符截断成 `...`。
- **汉化/追加 ROM**：`gen_rom.py` 生成 `rom.h` 时自动给出 `PB_ROM_COUNT`，
  超过 7 个会自动出现滚动（右侧上下小三角指示）。
- **自检**：菜单不依赖 PIO/DMA/时钟，所以在 PC 上就能渲染出来看布局：
  ```
  g++ -O2 -DPB_ROM_MENU_HOST -I . -I picoboy_bsp/include \
      tools/menu_preview.cpp rom_menu.cpp -o tools/menu_preview.exe
  tools/menu_preview.exe tools/menu.ppm 0 <rom1> <rom2> ...
  ```

## 16MB flash

SDK 默认的 `memmap_default.ld` 只声明 **2MB** FLASH（pico 板配置），
ROM 一旦超过 2MB 链接就会报 `region FLASH overflowed`。
`memmap_pico16m.ld` 是 SDK 1.5.1 `memmap_default.ld` 的副本，只把
`FLASH LENGTH` 从 `2048k` 改成 `16384k`，在 `CMakeLists.txt` 里用
`PICO_TARGET_LINKER_SCRIPT` 指定。RP2040 的 XIP 窗口本来就是 16MB
（0x10000000–0x10FFFFFF），所以 16MB 不需要额外改动。

当前镜像：`picoboy_gbc.bin` = 3,693,324 字节（`.uf2` = 7,387,136 字节），
`.rodata` 里绝大部分是 ROM 数据；离 16MB 上限还有约 12MB 空间。

## 相对花生 DMG 版的四处改动

1. **核心 API 变了**：`gb_init()` 多了 `gb_rom_read_16bit` / `gb_rom_read_32bit`
   两个回调（Walnut 的 32-bit DMA 路径要用）。本工程用逐字节拼接实现，
   避开 Cortex-M0+ 非对齐半字/字访问的限制。
2. **BSP 新增 `pb_push_rgb_line()`**：原 `pb_push_gb_line()` 只会查 4 档调色板，
   GBC 每像素颜色都不同，所以加了「直接喂 160 个 RGB565」的版本，
   1.5 倍最近邻、半幅乒乓、core1 推带全部复用原有逻辑。
3. **CGB 上色路径**：`gb->cgb.cgbMode` 为真时，像素值就是 `fixPalette` 的下标
   （BG 0x00–0x1F / OBJ 0x20–0x3F），直接查表；否则回落到 DMG 四档绿。
4. **锁帧**：加了 59.7275 fps（16743 µs）的帧节奏。不锁的话模拟会跑飞，
   硬件 PWM 音频（61.035 kHz 定速消费）会一直丢样本。

## BSP 回写：把做 GBC 时新增的能力交回 BSP 本体

做 GBC 时 BSP 缺两块东西：一是 GBC 每个像素颜色都不同，原来 `pb_push_gb_line()`
只能查 4 档调色板；二是菜单需要 2 倍字号和居中排版，原来只有 1 倍 `pb_text()`。
这些现在都写进 BSP 本体（`picoboy_bsp/`），两个 demo 也一并更新。

### 1. 显示：新增 `pb_push_rgb_line()`（`pb_display.cpp` / `pb_display.h`）

```c
void pb_push_rgb_line(const uint16_t *colors, uint8_t line);   /* 160 个 RGB565 */
```

与 `pb_push_gb_line()` 完全同构：水平 1.5 倍最近邻、垂直 1.5 倍、半幅乒乓、
core1 推带全部复用同一套，区别只在不查调色板。GBC 走这条，DMG 走原来的。

### 2. 文本：`pb_text_scaled` / `pb_text_width` / `pb_text_center`

| 接口 | 说明 |
|---|---|
| `pb_text(x, y, s, fg, bg)` | 1 倍，行为不变（现在是 `pb_text_scaled(..., 1, ...)` 的薄封装） |
| `pb_text_scaled(x, y, s, scale, fg, bg)` | scale=2 时每点画成 2×2（字符 6×10、步进 8px） |
| `pb_text_width(s, scale)` | 字符串像素宽度（末字符不含字距），画框/下划线用 |
| `pb_text_center(y, s, scale, fg, bg)` | 整屏水平居中 |

`pb_text` 旧实现是逐像素直写帧缓冲，新实现按 `scale` 分块填 `pb_fill_rect`；
scale=1 时像素结果完全一致——改动后重渲染菜单，两种选中态的 PPM 与改动前
**SHA256 逐字节相同**（`C828FA84…` / `0084263C…`）。

### 3. demo 更新

- `examples/demo.cpp`：标题走 `pb_text_center(2 倍)`，下面用 `pb_text_width()`
  算出等宽分隔线；底部 1 倍文本显示坐标和当前按下的键（顺带演示 `pb_input_read()`
  位掩码）；方块上还叠了一行 1 倍 "BOX"，底色跟着 A 键变。
- `examples/demo_dual.cpp`：一帧里同时用两条推线接口——上半屏 `pb_push_gb_line()`
  调色板棋盘 + 弹跳方块，下半屏 `pb_push_rgb_line()` 画 8 条色带、亮度由暗到亮，
  顺便当**面板色序/反色自检图**（红蓝互换、整体反相一眼能看出来）。文件头也写明
  流式模式下 `pb_fb` 被当作带池复用，不能混用 `pb_text` / `pb_display_flush`。

BSP 自带独立 CMake 工程，两个 demo 可以单独编译验证：

```powershell
cmake -S picoboy_bsp -B picoboy_bsp/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build picoboy_bsp/build
# 产物：picoboy_bsp/build/pb_demo.uf2（80,384 B）、pb_demo_dual.uf2（60,928 B）
```

对照原始花生工程 `peanut_gb_bsp1.1/picoboy_bsp`，本次共改 6 个文件：
`README.md`、`include/picoboy/pb_display.h`、`src/pb_display.cpp`、`src/pb_audio.cpp`、
`examples/demo.cpp`、`examples/demo_dual.cpp`；`board.h`、`pb_input`、`CMakeLists.txt` 未动。

## 全 C++ 改造

工程自身的源文件已经全是 C++（`main.cpp` / `rom_menu.cpp` / `picoboy_bsp/*.cpp` /
`minigb_apu.cpp` / `tools/*.cpp`），统一按 C++17 编译；`third_party/Walnut-CGB` 和
`minigb_apu` 的上游代码一行没改，直接按 C++ 编译通过。CMake 里的 C 语言只留给
pico-sdk / tinyusb 自己的 `.c` 库源码。

| 原文件 | 现在 | 说明 |
|---|---|---|
| `main.c` | `main.cpp` | 内容基本不用改 |
| `rom_menu.c` | `rom_menu.cpp` | `rom_menu.h` 不再需要 `extern "C"`（调用方也是 C++） |
| `minigb_apu/minigb_apu.c` | `minigb_apu.cpp` | 直接按 C++ 能编 |
| `tools/host_smoke.c` | `host_smoke.cpp` | `malloc` 要显式转 `uint8_t *` |
| `tools/menu_preview.c` | `menu_preview.cpp` | 预览工具的 BSP 桩要放进 `extern "C" { }` |

两条边界规则：

1. **BSP 的 C 接口**：`pb_*.h` 原本就有 `extern "C"` 包裹，保持不变——BSP 对外提供的是
   C 接口。工程内部（`main.cpp` ↔ `rom_menu.cpp`）两边都是 C++ 翻译单元，链接名一致，
   `rom_menu.h` 已经不需要桥接。唯一要写 `extern "C"` 的地方是**实现** BSP 接口的
   `.cpp`（`picoboy_bsp/src/*.cpp`、`tools/menu_preview.cpp` 的桩），否则定义是 C++
   链接名，和头文件里的声明冲突。
2. **`walnut_cgb.h` 是纯 C 的单头文件**：在 C++ 里 `#include` 之前必须先 `struct gb_s;`
   前置声明，并显式声明 `audio_read` / `audio_write` 两个钩子（C++ 没有隐式函数声明），
   顺序照 `main.cpp` 文件头那样写即可，头文件本身一行不用改。

`minigb_apu.h` 没有 `extern "C"`，两个 TU 都是 C++，链接名自然一致，不需要额外处理。

验证（本轮收尾：去掉 `rom_menu.h` 的 `extern "C"` 后重编）：

- 固件零警告编译通过与链接：`picoboy_gbc.bin` 3,693,324 B、`picoboy_gbc.uf2` 7,387,136 B；
  产物字节与去掉桥接前 **SHA256 完全相同**（`75DE0C7E…B1EA9E`），说明 `extern "C"`
  只是链接约定，没有影响任何代码生成。
- 菜单预览：`tools/menu_preview.exe` 用 g++ `-Wall -Wextra` 重编零警告，选中 0 号与 2 号
  两种状态的 PPM 与改造前 **SHA256 完全相同**（`C828FA84…F7F385` / `0084263C…19D62ADA`）。
- `host_smoke` 跑 `zelda_dx.gbc` 180 帧仍 `SMOKE OK`：`gb_init -> 0`、`cgbMode=1`、MBC5、
  4 个 RAM bank、22,895 次行回调、23 种颜色，与改造前一致。

## 构建

```powershell
$env:PICO_SDK_PATH = "E:\Program Files\Raspberry Pi\Pico SDK v1.5.1\pico-sdk"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

或直接跑 `.\build.ps1`（脚本里已把 SDK 的 cmake/ninja/gcc 加进 PATH）。
产物：`build/picoboy_gbc.uf2`（7.4MB，拖进 BOOTSEL 盘符烧录）、`.elf`、`.bin`、`.hex`。
rom.h 有 19MB 文本、3.6MB ROM，头一次编译 `main.cpp` 要一两分钟，之后增量很快。

## 更换 / 追加 ROM

```powershell
# 多个 ROM：一屏 7 个，多了自动滚动
python gen_rom.py rom.h a.gbc b.gb c.gb
cmake --build build
```

ROM 以 `const` 数组驻留 flash 的 `.rodata.pb_roms` 段。
**ROM 长度必须是 2 的幂**（64KB/128KB/1MB/2MB…）：`main.cpp` 用掩码做地址边界，
非 2 的幂会在开机直接报 `ROM SIZE NOT 2^N`。
注意 `rom.h` 只能被一个编译单元 include（数组是 `static`，多处 include 会把
几 MB 数据重复链接进 flash），所以菜单走 `pb_roms.h` 的轻量类型而非 `rom.h`。

## 硬件与引脚（见 `picoboy_bsp/include/picoboy/board.h`）

| 功能 | 引脚 | 说明 |
|---|---|---|
| LCD SCK / MOSI / CS / DC / RST / BL | 6 / 7 / 5 / 9 / 4 / 12 | ST7789，PIO+DMA，62.5MHz |
| 音频 | 11 | 单 GPIO PWM 当 DAC，12bit，DMA 环形缓冲 |
| 按键 A/B/X/Y | 18 / 19 / 17 / 16 | 高电平有效 |
| 方向 上/下/左/右 | 23 / 20 / 22 / 21 | 高电平有效 |

按键映射：板上 **X = Start、Y = Select**；A/B 与方向键同名对应。
Walnut 的 `direct.joypad` 低有效，每帧写 `~pad`，并重算 P1 + 每帧补一次手柄中断。

## 验证记录（2026-11-14，本机）

`tools/host_smoke.cpp` 用 mingw g++ 在 PC 上直接跑核心（不经硬件）：

| ROM | 结果 |
|---|---|
| `zelda_dx.gbc` | `gb_init -> 0`，CGB/MBC5/4 bay RAM，600 帧无错，23 种 RGB565 色 |
| `pocket_bomberman.gbc` | `gb_init -> 0`，CGB/MBC5，240 帧无错，33 种色 |
| `Spider-Man 2 …(USA,Europe).gbc` | `gb_init -> 0`，CGB/MBC5，150 帧无错，29 种色 |
| `dd3_arcade.gb` | `gb_init -> 0`，DMG/MBC1，150 帧无错，4 档绿 |
| `double_dragon.gb` | `gb_init -> 0`，DMG/MBC1，240 帧无错 |
| `mario_yoshi.gb` | `gb_init -> 0`，DMG/MBC1，150 帧无错 |
| `spiderman2.gb` | `gb_init -> 0`，DMG/MBC1，150 帧无错 |

`tools/menu_preview.cpp` 渲染菜单做布局自检（0 号/2 号选中两种状态都确认过）。
固件：`.bss` = 213,112 B / 264KB（余约 47KB）。

### 实机反馈修复：菜单按键音后持续「滴」

烧到机器上发现的第一个问题——菜单里按一下方向键就**一直滴**，进游戏也不停。根因在
BSP 的音频驱动：`pb_beep()` 改的是 PWM **载波**（wrap + clkdiv），而 DMA 环形缓冲仍在跑，
`pb_tone_stop()` 写的 `cc=0` 在下一次 wrap 就被 DMA 覆盖回 `PWM_MID`。载波还停在
720Hz，于是这个本意是"静音"的电平变成了一个 720Hz、约 3% 占空比的方波——
听起来就是持续滴声；进游戏后 `pb_audio_submit()` 的 PCM 同样被按 720Hz 载波抽出来，
所以继续滴。

修法（`picoboy_bsp/src/pb_audio.cpp`）：把音调与 DAC 流式**显式互斥**——

- `pb_tone_start()`：先关该通道的 DMA 完成中断（堵 abort 瞬间 ISR 把通道重挂的竞态）、
  关 slice、`dma_channel_abort()`、清中断标志，再改 wrap/clkdiv。
- `pb_tone_stop()`：还原 DAC 的 wrap/clkdiv、环形缓冲填 `PWM_MID`、`s_wp` 预热半满、
  重挂 DMA 并重新开中断。`pb_audio_play_sample()` 走同一套，播完交还给流式。
- `pb_audio_submit()` 加了兜底：发现流式被停掉且不在发声调，就自动装回 DAC。

这样 `pb_beep()` 只是 25/60ms 的短促一声，之后游戏音频正常。

## 已知限制

- **存档不落地**：卡带 RAM 只在内存里（最大 32KB），掉电即失。要做持久化得挂
  flash 扇区或 LittleFS；Walnut 的 `gb_get_save_size_s()` 已给出应存大小。
- **只有 32KB 存档空间**：8 bank（64KB / 128KB）的卡带会被截断，越界写入丢弃。
- **没有返回菜单**：进游戏后想换游戏只能复位（BSP 的 core1 推屏 worker 一旦启动
  就不带停机接口，异步 DMA 中途切回单核写屏容易撕裂，所以没做热切换）。
- **无 RTC**：MBC3 时钟从 0 起算（未调 `gb_set_rtc()`），时间类游戏会固定在零点。
- **面板反色按花生版配置**：`board.h` 的 `PB_LCD_INVERT=1` 表示面板自己发反色，
  初始化时发 `0x21` 抵消，所以固件里给的是真实 RGB565。若实机颜色整体反相，
  编译时加 `-DPB_LCD_INVERT=0` 重试。
- **含版权 ROM**：镜像内嵌的是本机已有的 ROM 文件，自行烧录、不要分发。
- RP2040 @250MHz 跑 CGB 的帧率未在实机标定；若掉帧先试
  `gb.direct.frame_skip = true`（30 fps）或降 `PB_DISPLAY_SCALE15=0`（160×144 直出）。
