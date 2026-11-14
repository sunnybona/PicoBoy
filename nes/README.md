# pico-nes

> **注意：本工程已并入 PicoBoy 整合工程**（`E:\PicoBoy`），与本目录配套的
> Game Boy 工程在 `../gbc`，板级代码只有共用的一份 `../bsp`，
> ROM 菜单由 `../bsp/src/pb_rom_menu.cpp` 提供（与 GB 同一套外观和字号）。
> 构建入口在上级目录：`cmake -S .. -B ../build`，产出 `pico_nes.uf2`。
> 本文以下内容仍按「独立工程」口径描述（路径写成 `E:\Pico-Nes`），
> 结构说明请以 `../README.md` 为准。

RP2040 + PicoBoy（240×240 ST7789 掌机）上的 **NES 模拟器**。

- 核心：InfoNES（`https://github.com/jay-kumogata/InfoNES`，已做逐行渲染改造）
- 板级：本地 `picoboy_bsp`（`C:\Users\bobo\Desktop\picoboy_gbc\picoboy_bsp`，已加一条 NES 推屏通道）
- ROM 编译进 flash，上电出菜单选盘

```
build\pico_nes.uf2  ← 烧这个
```

---

## 1. 快速开始

```powershell
# 1) 把 .nes 丢进 roms\（现在里面已经放了 8 个游戏 + 1 个自检 ROM）
# 2) 构建
.\build.ps1
# 3) 板子按住 BOOTSEL 插 USB，把 build\pico_nes.uf2 拖进去
```

`build.ps1` 会自动设好 SDK 路径（`E:\Program Files\Raspberry Pi\Pico SDK v1.5.1`），
重新 configure 时会把 `roms\*.nes` 重新打包成 `build\nes_rom.h`，最后打印 `.elf` 的段大小。

没有 ROM 也能编译（会链接成功但上电显示 `NO ROM`）；加了 ROM 才能热闹起来。

---

## 2. 实测占用（已构建验证）

| 项目 | 大小 | 说明 |
|---|---|---|
| `.text` | 191,976 B（187 KiB） | K6502 + PPU + 133 个 mapper + BSP（含 pb_settings）+ SDK |
| `.rodata` | **1,865,612 B（1,822 KiB）** | **内嵌的 9 份 ROM（1816 KiB）** + 调色板点阵等 |
| `.bss` | **234,960 B（229 KiB）** | 见下面内存预算表 |
| RAM 总量 | 264 KB | 余量约 25 KiB（两个核各 4 KiB 栈已从这里划走） |
| flash | 16 MB | 现用 **3.94 MB**（uf2 4,127,232 B），其余是 ROM 空间，最后一扇区留给设置 |

`arm-none-eabi-size -A build\pico_nes.elf` 即可复现。ROM 列表见 `roms\README.md`。

---

## 3. 操作

| PicoBoy | NES 手柄 |
|---|---|
| A | A |
| B | B |
| Y | Select |
| X | Start |
| 十字键 | 十字键 |

- 菜单：上下选 ROM，A / X 开始，**Y 进设置页**
- 游戏中：**X + Y 同时按住约 1 秒** = 软复位（等价 NES 的 Start+Select）

### 设置页（亮度 / 音量）

这一页不是本工程自己写的，而是 **BSP 里的 `pb_settings` 模块**（`picoboy_bsp/src/pb_settings.cpp`），
GB 工程同样在用。本工程只负责在菜单里给它一个入口：

```cpp
pb_settings_init();        // 开机：读 flash + 应用（默认 9 档亮 / 8 档音量）
...
pb_settings_menu();        // 菜单按 Y：进入全屏设置页
pb_settings_save();        // 返回后落盘（此刻还是单核，擦 flash 安全）
```

菜单界面按 **Y** 进入（ROM 菜单、NO ROM 页、ROM ERROR 页都能进）：

| 键 | 作用 |
|---|---|
| 上 / 下 | 在 BRIGHT（亮度）和 VOLUME（音量）之间切换 |
| 左 / 右 | 减 / 加一档 |
| A / B / Y | 存盘并返回菜单 |

- 亮度 1–10 档，默认 9（≈ 229/255，和原 GB 工程的 `pb_backlight(220)` 一个观感）。
  **没有 0 档**：背光全关之后屏上什么都看不见，就调不回来了。
- 音量 0–10 档，默认 8（原 GB 工程写的就是 8）。调音量时会响一声 1200 Hz 短音，
  直接听当前音量有多大。
- 设置存在 **flash 最后一扇区（0xFFF000）**，断电不丢；值没变就不擦写，省寿命。
  只在菜单阶段读写——那时 core1 推屏 worker 还没起、中断一关就安全，游戏跑起来后
  完全不碰 flash。
- 进游戏之后亮度和音量就固定了，**游戏中不能调**：`pb_fb` 已经被推屏带池接管，
  画不了菜单。而 X+Y 的软复位是 `InfoNES_Reset()`（复位当前这局游戏），不会回菜单。
  要调只能断电重开。这个取舍是刻意的——为了给 NES 腾出 115 KB 帧缓冲，
  游戏期不能再借屏画 UI。

想恢复默认：进设置页手动调回 9 / 8，或者用 `picotool erase` 整片擦一次
（重烧 uf2 不会动最后一扇区，设置会留着）。

> 构建侧有个必须项：`PICO_FLASH_SIZE_BYTES=16777216`。这个宏现在挂在 `picoboy` 库上
> （`target_compile_definitions(... PUBLIC)`），所以 `pb_settings.cpp` 和 `flash.c` 看到的是
> 同一个值。SDK 默认按 2 MB 算，而固件本身 3.94 MB，且 `flash_range_erase/program` 会
> 拿这个宏做 `hard_assert(offs + count <= PICO_FLASH_SIZE_BYTES)`——不改成 16 MB，
> 写 0xFFF000 会直接 assert 死机。

---

## 4. 设计要点

### 4.1 显示：为什么必须逐行

NES 输出 256×240，屏幕是 240×240，所以**左右各裁 8 列**、1:1 不缩放（省 CPU，也不糊像素）。

上游 InfoNES 是"整帧缓冲"路线：`WorkFrame[256*240]`（`WORD`，120 KB）。PicoBoy 的
`pb_fb` 本身已经占了 115 KB（PIO+DMA 的唯一大缓冲），两个加起来直接爆 264 KB。
所以这里把核心改成**只保留一条扫描线**：

```c
WORD WorkFrame[ NES_DISP_WIDTH + 8 ];   // 256 + 8
```

`+8` 不是冗余写错：精灵合成时会 `pPoint -= (256 - PPU_Scr_H_Bit)`，最远写到
index `255 + H_Bit`，`H_Bit` 最大 7，所以单行缓冲必须接住这 8 个字节的越界写。

流水线：

```
InfoNES_HSync()  ──每行──>  InfoNES_DrawLine()   // 渲染到 WorkFrame
                              └> InfoNES_LoadLine(nLine)   // pico 平台层：裁 8 列 + RGB555→RGB565
                                    └> pb_push_nes_line()    // 240 像素塞进 BSP 带池
                                          └> core1 推屏 worker ──> PIO/DMA ──> ST7789
```

送屏用的是 BSP 新加的 NES 带池：**每带 60 行、共 4 槽**，
`60 × 4 × 120 words = 115200 B`，正好把 `pb_fb` 吃满（GB 通道一行没动，两条通道由 core1
的 worker 轮流服务）。

### 4.2 音频

pAPU 每帧固定产 `735` 个样本（44100 / 60）。BSP 的 PWM 载波是 `61035 Hz`，一帧
（16639 µs）需要约 1015.6 个样本，取 **1016**：

- 735 → 1016 线性插值，误差 0.05%，每帧重新对齐，不会累积漂移
- 5 路波形直接相加（静音时全 0，没有固有大直流偏置）
- 一阶高通去直流（截止约 38 Hz），再 `×24` 限幅到 ±32000
- `pb_audio_submit()` 非阻塞丢进 2048 样本的环形缓冲

### 4.3 内存预算（`.bss` 235 KB 的构成）

| 缓冲 | 字节 | 谁的 |
|---|---|---|
| `pb_fb` | 115200 | BSP 显示（也是 NES 带池） |
| `ChrBuf` | 32768 | InfoNES：CHR RAM 展开 |
| `PPURAM` | 16384 | InfoNES：PPU 地址空间 |
| `ApuEventQueue` | 16384 | pAPU 事件队列（**已从 15000 条砍到 2048 条**，见补丁 5） |
| `RAM` / `SRAM` | 8192 + 8192 | InfoNES：CPU 内存 / 卡带存档 |
| `Map19_Chr_Ram` / `Map188_Dummy` | 8192 + 8192 | mapper 19 / 188 私有 |
| `wave_buffers` | 3675 | pAPU 波形 |
| `WorkFrame` | 528 | 单行缓冲 |
| 其它（表、BSP 状态、SDK） | ~10 KB | |

两个核的栈不在这 235 KB 里：分别在 `SCRATCH_X` / `SCRATCH_Y` 两个 4 KB bank 里，
`CMakeLists.txt` 用 `target_compile_definitions` 把 `PICO_STACK_SIZE`（每核）设成 `0x1000`。
SDK 1.5.1 里这个值是 `platform.h` 的编译宏而不是 CMake 缓存变量，只 `set(...)` 不生效。

### 4.4 帧节拍

`PB_NES_FRAME_US = 16639`（2C02 的 60.0988 Hz）。主循环 `time_us_64()` 对齐，
落后一帧以上就直接重新起算（宁可偶尔掉一帧，也不让音画累积漂移）。

---

## 5. 对上游 InfoNES 的补丁清单

改的都是 `infones\` 下的上游源码，每处都有 `pico-nes 补丁` 注释：

| # | 文件 | 改动 |
|---|---|---|
| 1 | `InfoNES.cpp` | `WorkFrame[256*240]` → `WorkFrame[256+8]` |
| 2 | `InfoNES.cpp` | 4 处行基址 `&WorkFrame[Scanline*256]` → `WorkFrame` |
| 3 | `InfoNES.cpp` / `InfoNES_System.h` | `InfoNES_DrawLine()` 之后调新增的 `InfoNES_LoadLine(nLine)` |
| 4 | `InfoNES.cpp` / `InfoNES.h` | `InfoNES_Cycle()` 拆成 `InfoNES_ScanlineStep()`（跑一条扫描线）+ `InfoNES_RunFrame()`（跑一整帧后返回）；`InfoNES_Cycle()` 保留原语义 |
| 5 | `InfoNES_pAPU.h` / `InfoNES_pAPU.cpp` | `APU_EVENT_MAX` 15000 → 2048（120 KB → 16 KB），并给 `APU_WRITEFUNC` 补越界保护（上游没有，写满会踩内存） |
| 6 | `InfoNES_Mapper.cpp` | 删掉 `DRAM[0xA000]`（40 KB）；摘掉 mapper **5 / 6 / 21 / 85 / 235**（私有缓冲 64+32+40+256+40 KB） |
| 7 | `K6502_rw.h` | 加了 `PB_NES_DEBUG_PPU` 调试开关：定义后打印每次 `$2007` 写入的地址/数据 |

`InfoNES_LoadFrame()` 顺手改成"把最后一条带交给 core1"（`pb_display_nes_end_frame()`）。

**没被改的**：K6502 的 6502 指令实现、PPU 的精灵/背景合成逻辑、mapper 的分页代码——
这些保持上游原样，只是喂给它们的内存变小了。

---

## 6. 平台层（`src\InfoNES_System_Pico.cpp`）

InfoNES 把所有"系统相关"集中在这 16 个函数里，这里全部落到 BSP：

- `InfoNES_ReadRom()`：ROM 已经在 flash 里（`const` 数组，XIP 读，零拷贝）。只解析 16 字节 iNES 头、
  校验 `NES\x1a`、算 PRG/CHR 大小、处理 trainer 的 512 字节偏移，然后把 `ROM`/`VROM` 指向 flash。
  ROM 只被读，不被写，所以指过去是安全的。
- `InfoNES_LoadLine()`：裁 8 列 + RGB555→RGB565：
  `((v & 0x7FE0) << 1) | ((v & 0x0200) >> 4) | (v & 0x001F)`（R/B 直通，G 把第 5 位补成第 6 位）
- `InfoNES_PadState()`：8 个键 → `$4016` 位序（A B Select Start Up Down Left Right）
- `InfoNES_SoundOutput()`：见 4.2
- 其余（`MemoryCopy/Set`、`Menu`、`Wait`、`MessageBox`、`DebugPrint`）都是直通/no-op

---

## 7. 对 BSP 的改动（`picoboy_bsp\`）

GB 通道一行没动，新增一条并行通道：

- `include\picoboy\pb_display.h`：新增
  `pb_display_nes_begin_frame()` / `pb_display_nes_end_frame()` / `pb_push_nes_line(colors, line)`
- `src\pb_display.cpp`：`NES_BAND_ROWS 60` / `NES_BAND_SLOTS 4` / `NES_WORDS_PER_ROW 120` 的带池，
  `nes_band_*` 内部函数，`core1_push_worker()` 改成"先看 NES 环、再看 GB 环、都没活才 `__wfe()`"，
  `pb_display_wait_idle()` 也把两个环都算进去
- 用法约束：带池吃满 `pb_fb`，**和帧缓冲 / 文字绘制互斥**——菜单画完再启推屏 worker

另外同步了一份 **BSP 自带模块**（不再由本工程自己维护）：`include\picoboy\pb_settings.h` +
`src\pb_settings.cpp`（亮度/音量 + flash 持久化 + 设置页）。它是通用能力，GB 工程也能用，
本工程只是链接它、在菜单里挂一个 Y 入口。`CMakeLists.txt` 里给它加了 `hardware_flash` 和
`PICO_FLASH_SIZE_BYTES=16777216`（PUBLIC，保证库和 `flash.c` 一致）。

> GB 的 1.5× 放大（240/216、120 words/row、108 行带）是编译期常量，几何和 NES 的 240×240
> 1:1 对不上，复用它得把这套常量改成运行期可变，会把 GB 路径拖慢，所以这里选择并列两条通道。

---

## 8. 加 ROM

```powershell
copy 你的游戏.nes roms\
.\build.ps1
```

**文件名就是菜单显示名**，必须用 ASCII（板子只有 3×5 点阵，中文会被打成空格）。
菜单按文件名字母序排，14 行一屏、可滚动。

`tools\gen_nes_rom.py` 在 configure 阶段扫描 `roms\*.nes`：

- 解析 iNES 头取 mapper / PRG / CHR 大小（PRG 页 = 16 KB、CHR 页 = 8 KB）
- 生成 `build\nes_rom.h`：`static const uint8_t nes_rom_N[] __attribute__((aligned(4)))`（4 字节对齐，避免 XIP 非对齐读的问题）+ `const pb_nes_rom_t pb_nes_roms[]`
- 坏文件/非 iNES 会跳过并打印，空目录生成空表（工程照样编译）

**当前内置 9 份**（8 个游戏 + 1 个自检），合计 1816 KiB，逐份的 mapper / 实测画面见 `roms\README.md`。

> flash 里给 ROM 留的空间上限约 **15.9 MB**：最后一扇区 0xFFF000 要留着存设置，
> 固件本体也在 `build\pico_nes.elf` 里。ROM 总量逼近这个数时，链接会因为放不下而报错。

### 支持的 mapper

保留的 **133 个**，日常能打的基本都在：0(NROM) 1(MMC1) 2(UxROM) 3(CNROM) 4(MMC3)
7(AxROM) 9(MMC2) 11(ColorDreams) 13 16 18 19(N163) 23 25(VRC4) 26 32 33 34 64 65 66 67 68 69
70 71 73 75 76 78 79 80 87 88 90 93 95 97 99 113 114 118 119 122 140 154 180 182 184 185
187 188 189 191 193 194 199 206 207 210 225 227 228 229 231 232 233 234 240 241 242 243 244
245 246 248 249 251 252 255 等。

**摘掉的**：5(MMC5) 6(FFR) 21(FDS) 85(VRC7) 235 —— 见补丁 6，RAM 不够。
遇到这些 ROM，`InfoNES_Load()` 会失败，屏幕显示 `ROM ERROR`。

### 存档

`SRAM` 目前只在 RAM 里（掉电就没了）。要真正存档，注意 **flash 最后一扇区（0xFFF000）
已经被亮度/音量设置占用**，所以存档区要放在它前面（比如倒数第二扇区 0xFFE000），
写之前还得 `pb_display_wait_idle()`、停 core1 推屏、`save_and_disable_interrupts()`
——XIP 停摆的那几毫秒里任何取指中断都会崩。

---

## 9. 自检：不开真机也能验

### 9.1 自制自检 ROM（`roms\pb_nes_test.nes`）

```powershell
python tools\make_test_rom.py     # 重新生成（改配色/图案时用）
```

全屏是 32×32 像素的方块，每块再分 16×16 四个象限：**白 / 红 / 绿 / 蓝**。
它把整条链路串了一遍：CPU 跑代码 → `$2006/$2007` 写调色板和属性表 → PPU 取图块
→ 属性选调色板 → 逐行输出 → 裁边 → RGB565。画面长这样 = 链路通。

> 做这个 ROM 踩了两个坑，都写进注释了，对以后写 homebrew 有用：
> 1. **色号 0 是背景色**：图块像素全 0 时，四个象限都取 `$3F00`（而且不走属性调的调色板），
>    全屏一个颜色。所以图块数据得让像素 = 1。
> 2. **`$3F10` 会镜像到 `$3F00`**：把精灵调色板整段写成 0 会把背景的 0 号色一起冲掉
>    （InfoNES 实现镜像时会把 4 个"0 号色"一起改）。所以只写 `$3F00-$3F0F`。

### 9.2 PC 侧跑核心（`tools\hosttest\`）

把核心和平台层搬到 PC 上跑，导出 PPM 看画面：

```powershell
E:\mingw64\bin\g++.exe -std=c++17 -O2 -I infones -I include `
    infones\InfoNES.cpp infones\InfoNES_Mapper.cpp infones\InfoNES_pAPU.cpp `
    infones\K6502.cpp tools\hosttest\host_test.cpp -o build\host_test.exe

build\host_test.exe roms\pb_nes_test.nes build\frame.ppm 60
```

它会打印：每帧 `DrawLine` 多少行、`LoadFrame` 多少次、`PalTable`、属性表、
`SoundOutput` 每帧多少个样本。**实测结果**：

```
帧  0：DrawLine 240 行，LoadFrame 1 次
音频：SoundOutput 调用 60 次，共 44100 样本（每帧 735.0）
颜色种类：4   （白/红/绿/蓝 各 13440~15360 像素）
```

`build\frame.png` 就是导出的画面。`build\shot_sheet.png` 是 8 个游戏的截图拼图。
第 5 个参数控制自动按 Start（默认 1，每 20 帧按 20 帧），`0` 关掉；逐份实测结果见 `roms\README.md`。

### 9.3 PPU 调试开关

```powershell
cmake -S . -B build -DCMAKE_CXX_FLAGS=-DPB_NES_DEBUG_PPU
```

定义后每次写 `$2007` 都会打印 `地址 = 数据`，用来查"游戏写了调色板/属性但画面不对"。

---

## 10. 性能旋钮

| 旋钮 | 位置 | 说明 |
|---|---|---|
| 主频 | `board.h` `PB_SYS_CLOCK_KHZ`（250 MHz） | 刷屏 SPI 是 62.5 MHz，NES 每帧 2.98 万 CPU 周期 + 5.76 万像素，超频是必须的 |
| 优化等级 | `CMakeLists.txt` `-O3` | 已给 `infones` 和主程序都加上 |
| 帧节拍 | `include\pb_nes.h` `PB_NES_FRAME_US` | 默认 16639（60.0988 Hz），和真机音高一致 |
| 音频增益 | `InfoNES_System_Pico.cpp` 里 `y * 24` | 觉得小声可以调大，到 ±32000 为止 |
| 音量 | `main.cpp` 设置页（VOLUME 0..10 档，默认 8） | 存在 flash 最后一扇区，见 3 |
| 亮度 | `main.cpp` 设置页（BRIGHT 1..10 档，默认 9） | `pb_backlight(档 * 255 / 10)` |
| 裁边 | `pb_nes.h` `PB_NES_CROP_X` | 看不够就把裁边改小（但屏幕只有 240 宽） |
| 带数 | `pb_display.cpp` `NES_BAND_SLOTS` | 4 槽刚好吃满 `pb_fb`；改小会减少并行度 |

如果帧率不够（还没上真机测），优先按顺序动：`-O3` → 主频 → `InfoNES_DrawLine` 里
关闭精灵/背景的 8 像素左边缘处理（`PPU_Scr_H_Bit` 那套 loopy 逻辑）→ 降低采样率。

---

## 11. 已知限制

- **没有存档**（SRAM 不落 flash，见 8）
- **亮度和音量只能在菜单阶段调**：进游戏后 `pb_fb` 被推屏带池接管，画不出设置页；
  X+Y 软复位只是 `InfoNES_Reset()`，不会回菜单。要调得断电重开
- **mapper 5/6/21/85/235 不支持**（RAM 不够）
- **左右各裁掉 8 像素**：少数游戏的标题文字/边角 HUD 会被切掉一截（例如绿色兵团的
  `YOUR MISSION` 右侧就被裁了）。要全画面只能缩小比例，代价是像素糊
- 单声道、无低通滤波（pAPU 的输出直出，可能是晶体管味的方波，也可能是刺耳的）
- 只映射 1 号手柄，2 号手柄恒 0；没有连发键
- 没有 FPS 显示 / 即时存档 / 快进
- 菜单文字用的是 BSP 的 3×5 点阵，只认 `0-9 A-Z . : - / _ ? ! +`，其它字符会被替换成空格
- X+Y 复位是主循环里数 60 帧，游戏卡死（不返回到 HSync）时按不出效果
- ROM 全在 flash 里走 XIP（缓存 16 KB），PRG 大范围随机读的游戏可能因此掉帧；
  9 份 ROM 时 flash 用掉 3.9 MB / 16 MB，加 ROM 还很宽裕

---

## 12. 目录结构

```
E:\Pico-Nes
├─ build.ps1                一键构建（configure + build + 打印 .elf 段大小）
├─ CMakeLists.txt
├─ main.cpp                 菜单 + 主循环（帧节拍、复位组合键）
├─ include\pb_nes.h         屏幕/裁边/帧率常量、ROM 表结构
├─ src\InfoNES_System_Pico.cpp   平台层（显示/输入/音频/ROM）
├─ infones\                 InfoNES 上游核心 + 133 个 mapper（已打补丁，见 5）
├─ picoboy_bsp\             BSP 副本（已加 NES 推屏通道 + pb_settings，见 7）
├─ tools\gen_nes_rom.py     把 roms\*.nes 打成 build\nes_rom.h
├─ tools\make_test_rom.py   生成自制自检 ROM
├─ tools\hosttest\host_test.cpp  PC 侧跑核心 + 导出 PPM
├─ tools\uipreview\ui_preview.cpp  PC 侧按真实点阵字体预览菜单/设置页排版（导出 PPM）
├─ roms\                    放 .nes（现含 8 个游戏 + 自检 ROM，见 roms\README.md）
├─ memmap_pico16m.ld        SDK memmap 副本，FLASH 改成 16384k
└─ pico_sdk_import.cmake
```
