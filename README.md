# PicoBoy

RP2040 + ST7789（240×240）掌机上的两个模拟器，同一个工程、同一份板级支持包：

| target | 模拟器 | 核心 | 产出 |
|---|---|---|---|
| `picoboy_gbc` | Game Boy / Game Boy Color | Walnut-CGB + minigb_apu | `build/picoboy_gbc.uf2` |
| `pico_nes` | NES / FC | InfoNES（已改逐行渲染） | `build/pico_nes.uf2` |

两个固件互不干扰，各烧各的；ROM 都编进 flash，上电出菜单选盘。

---

## 1. 目录结构

```
PicoBoy/
├── CMakeLists.txt          顶层：两个 target + 共用库
├── build.ps1               一键构建（自动找 SDK、首次 configure、打印 uf2 大小）
├── memmap_pico16m.ld       SDK memmap 副本，FLASH 改 16384k（本机 16MB）
├── pico_sdk_import.cmake
├── bsp/                    ★ 唯一的板级支持包，两个 target 都链它
│   ├── CMakeLists.txt      只产出一个 picoboy 静态库
│   ├── include/picoboy/    board.h / pb_display.h / pb_audio.h / pb_input.h
│   │                       pb_settings.h / pb_rom_menu.h / font3x5.h
│   ├── src/                pb_display.cpp / pb_audio.cpp / pb_input.cpp
│   │                       pb_settings.cpp / pb_rom_menu.cpp / st7789_lcd.pio
│   └── examples/           独立 demo（不参与整合工程构建，见 bsp 内注释）
├── gbc/                    Game Boy 那边
│   ├── main.cpp            前端：ROM 回调、逐行上色、按键、帧节奏
│   ├── rom_menu.cpp/.h     ★ 只剩适配层：把 ROM 表转成共用菜单项
│   ├── pb_roms.h           内嵌 ROM 表的数据类型
│   ├── roms/               ★ 7 份 .gb/.gbc 源文件 + roms.list（菜单顺序）
│   ├── tools/gen_gb_rom.py roms/ -> build/gb_rom.h
│   ├── minigb_apu/         APU
│   └── third_party/Walnut-CGB/walnut_cgb.h   单头文件核心
├── nes/                    NES 那边
│   ├── main.cpp            前端：菜单、帧节拍、复位组合键
│   ├── include/pb_nes.h
│   ├── src/InfoNES_System_Pico.cpp   平台层（显示/输入/音频/ROM）
│   ├── infones/            InfoNES 上游 + 133 个 mapper（已打补丁）
│   ├── roms/               9 份 .nes，configure 时打包成 nes_rom.h
│   └── tools/gen_nes_rom.py
└── tools/
    └── menu_preview.cpp    PC 侧菜单渲染自检（两边同一份渲染代码）
```

**BSP 只有 `bsp/` 这一份。** `gbc/` 和 `nes/` 里都没有 BSP 副本——这是这次整合的重点。

---

## 2. 构建

```powershell
.\build.ps1                      # 两个都编
.\build.ps1 -Target pico_nes     # 只编 NES
.\build.ps1 -Clean               # 清掉 build/ 重新 configure
.\build.ps1 -Dist                # 编完顺手把两个 .uf2 拷到 dist/
```

脚本会自己找 SDK，查找顺序：`$env:PICO_SDK_PATH`（指向 `pico-sdk` 或其父目录都认）
→ `C:\Program Files\Raspberry Pi\Pico SDK*`、`E:\Program Files\...`
→ `%USERPROFILE%\pico-sdk`、`%USERPROFILE%\.pico-sdk`、`C:\pico\pico-sdk`。
找到 SDK 安装包（自带 `cmake\bin`、`ninja`、`gcc-arm-none-eabi\bin`）就自动加进 PATH；
如果 SDK 是纯 git clone、工具链另装，脚本退回到用 PATH 上的 `cmake`/`ninja`/`arm-none-eabi-gcc`。

运行时会先打印它认到的三个工具的绝对路径，对不上就直接看这几行。

手工构建（等价）：

```powershell
$env:PICO_SDK_PATH = "D:\wherever\pico-sdk"    # 指向 pico-sdk 本身
# 若 cmake / ninja / arm-none-eabi-gcc 不在 PATH，先自己加进去
cmake -S . -B build -G Ninja
cmake --build build
```

CMake 的日志走 stderr 会被 PowerShell 包成 `NativeCommandError`，看 `$LASTEXITCODE` 判断成败，别被红字吓到。

---

## 3. 菜单：两个工程共用同一份渲染

菜单的实现不在 `gbc/` 也不在 `nes/`，而在 **`bsp/src/pb_rom_menu.cpp`**：

```c
typedef struct {
    const char *name;   /* 列表项文本 */
    const char *info;   /* 选中时底部信息行 */
} pb_menu_item_t;

typedef struct {
    const char *title;      /* 标题栏主标题，2 倍字形 */
    const char *subtitle;   /* 标题栏副标题，1 倍字形 */
    const char *hint;       /* 底部提示行，1 倍字形 */
    uint16_t    info_color;
} pb_menu_style_t;

int  pb_rom_menu_select(const pb_menu_style_t *, const pb_menu_item_t *, int count);
void pb_rom_menu_render(const pb_menu_style_t *, const pb_menu_item_t *, int, int, int);
void pb_rom_menu_splash(const char *name);
void pb_rom_menu_set_setup_cb(void (*cb)(void));   /* Y 键 -> 亮度/音量设置页 */
```

两边的入口只剩「把各自的 ROM 结构转成 `pb_menu_item_t[]`」这一件事：
`gbc/rom_menu.cpp`（名字砍区域后缀、信息行填 `容量 MBC 机型`）、
`nes/main.cpp` 的 `rom_menu()`（信息行填 `MAPPER n PRG nK CHR nK`）。

### 外观参数（取自 GBC 定版，两边逐像素一致）

| 元素 | 字号 | 位置 / 尺寸 | 颜色 |
|---|---|---|---|
| 背景 | — | 整屏 | `08,10,08` |
| 标题栏色块 | — | `(0,0) 240×34` | `0C,22,10` |
| 标题 | **2 倍** | 居中，y=7 | 黄 `F8,C8,38` |
| 副标题 | **1 倍** | 居中，y=23 | 青 `00,FF,FF` |
| 列表项 | **2 倍** | x=14，y=42+r×22，`"序号 名字"` | 未选 `C8,D4,C0` |
| 高亮块 | — | `(6,y-6) 228×20` + 左 3px 黄条 | `24,56,24` |
| 滚动箭头 | — | x=228，上 y=40 / 下 y=190 | `68,80,68` |
| 信息行 | **1 倍** | 居中，y=201 | 青 |
| 提示行 | **1 倍** | 居中，y=216 | `68,80,68` |

字号就是你要的那套：**标题 2 倍、列表项 2 倍、其余 1 倍**，没有 3 倍字。

### 交互（同一套手感）

- 上/下：按下立刻走一格；按住 0.4 s 后每 90 ms 连发
- **菜单全程静音**：移动光标、确认都不发声（旧版的 720 Hz 翻页声和 1040 Hz 确认声都已去掉）
- 确认键 `A` 或 `X`
- `Y` 进亮度/音量设置页（值存 flash，掉电不丢）
- 一屏 7 行，超出出箭头；选完进游戏前统一 `LOADING + 游戏名`

### 视觉验收

`tools/menu_preview.cpp` 把 `bsp/src/pb_rom_menu.cpp` 编进 PC 程序，用两套真实数据渲染：

```powershell
g++ -O2 -DPB_ROM_MENU_HOST -I bsp\include tools\menu_preview.cpp bsp\src\pb_rom_menu.cpp -o build\menu_preview.exe
build\menu_preview.exe build\menu_gbc.ppm gbc 2
build\menu_preview.exe build\menu_nes.ppm nes 2
```

两张 240×240 图逐像素比对：只有文字（标题/条目/信息行）和「NES 多出来的下箭头」不同，
色块、行距、字号、高亮条的位置完全重合。

---

## 4. 换 ROM（两边同一套流程）

两个模拟器现在都是「ROM 源文件放目录 → configure 时打包 → 生成物落在 `build/`」，
工程目录里不再存任何已生成的大头文件。

**GB**：把 `.gb` / `.gbc` 丢进 `gbc/roms/`，然后重新 configure：

```powershell
.\build.ps1 -Clean
```

`gbc/tools/gen_gb_rom.py` 会扫目录、打印每份 ROM 的 MBC 类型和 CGB/DMG，
打包成 `build/gb_rom.h`。菜单显示名取文件名（下划线换成空格，例如
`zelda_dx.gbc` → `zelda dx`）。

**菜单顺序**由 `gbc/roms/roms.list` 决定（每行一个文件名，`#` 开头是注释）。
不想要这个文件就删掉，脚本会退回按文件名字典序排；表里没列到的 ROM 会追加到末尾，
不会悄悄丢。

**NES**：把 `.nes` 丢进 `nes/roms/`，同样是重新 configure：

```powershell
.\build.ps1 -Clean
```

`nes/tools/gen_nes_rom.py` 校验 iNES 头、读出 mapper / PRG / CHR 大小再打包，
菜单名取自文件名（大写化、非法字符变空格）。

两边菜单都最多列 32 项（`gbc/rom_menu.cpp` 的 `MENU_MAX_ROMS`、
`nes/main.cpp` 的 `PB_MENU_MAX_ROMS`），超过一屏 7 行自动出箭头。

> 迁移备注：`gbc/roms/` 里现有的 7 份 ROM 是从旧的 `rom.h` 里还原出来的
> （`tools/unpack_gb_rom.py`，一次性工具）。还原后重新打包出的
> `pb_roms[]` 表与旧头文件逐项相同（名字、大小、变量名都对得上），
> 固件大小也没变。以后直接往 `roms/` 里放文件就行，不需要这个脚本。

---

## 5. 资源占用

| | `.text` | `.bss` | uf2 |
|---|---|---|---|
| `picoboy_gbc` | 3,696,632 B（3.53 MB，其中 3.4 MB 是 7 份 GB ROM） | 217,372 B（212 KB） | 7,393,280 B |
| `pico_nes` | 2,064,464 B（1.97 MB，其中 1,822 KB 是 9 份 NES ROM） | 240,564 B（235 KB） | 4,129,280 B |

RAM 共 264 KB。两个固件里最大的静态块都是 `pb_fb`（240×240 RGB565 = 115,200 B）；
NES 另有 `ChrBuf` 32 KB、`PPURAM` 16 KB、`ApuEventQueue` 16 KB，GB 另有 `gb` 结构 49 KB、
`cart_ram` 32 KB。两边都还剩 25 KB 以上余量。

flash 16 MB，设置区固定在最后一扇区 `0xFFF000`（`pb_settings`），不吃固件空间。

---

## 6. 与原工程的关系

`C:\Users\bobo\Desktop\picoboy_gbc` 和 `E:\Pico-Nes` **都原样保留**，没有移动或删除任何文件。
`PicoBoy/` 是它们的归并副本，改动如下：

- `bsp/` 用的是 NES 那份（同时含 GB 1.5× 推屏通道、NES 带池、`pb_settings`），去掉 `project()` 后由顶层 `add_subdirectory` 引入
- 新增 `bsp/src/pb_rom_menu.cpp` + `bsp/include/picoboy/pb_rom_menu.h`（共用菜单）
- `gbc/rom_menu.cpp` 从「自己画」改成「转成菜单项」；`gbc/main.cpp` 的 `pb_backlight(220)`/`pb_audio_set_volume(8)` 换成 `pb_settings_init()`
- `gbc/` 里的 `rom.h`（19.5 MB 生成物）和旧 `gen_rom.py` 已删除，换成 `gbc/roms/` + `gbc/tools/gen_gb_rom.py`，与 NES 侧形式一致；7 份 ROM 从旧 `rom.h` 还原（`tools/unpack_gb_rom.py`）
- `nes/main.cpp` 的 `rom_menu()` 整段删除，改成调共用模块；加 `pb_rom_menu_splash()`
- 原来的 `draw_text()`（小写转大写 + 过滤字符）在 NES 侧只留 `show_message()` 用

后续要改菜单外观，只改 `bsp/src/pb_rom_menu.cpp` 一处，两个固件同时生效。

---

## 7. 已知限制

- ROM 增删要重新 configure（`.\build.ps1 -Clean`），增量 `--build` 不会重跑打包脚本
- 设置页只能在菜单里调（Y），进游戏后不能调
- 菜单文案里的空位靠 3×5 点阵的字距挤出来，标题最多 12 个字符再长会顶到边缘
- 两个 target 共用 `memmap_pico16m.ld`，改 flash 布局要同时考虑两个固件

---

## 8. 换一台电脑继续改

整个 `PicoBoy` 文件夹就是完整工程，**不含 `build/`**（那是生成物，81 MB，删了没事）。
拷过去直接能编，工程里没有一处硬编码本机路径——只有 README 举例里出现过本机目录。

目标机上需要装好：

| 东西 | 用途 | 必须 |
|---|---|---|
| Pico SDK 1.5.x（含 `pico-sdk`、cmake、ninja、arm-none-eabi-gcc） | 编固件 | 是 |
| Python 3（标准库就够，不用装 PIL） | `gen_gb_rom.py` / `gen_nes_rom.py` 打包 ROM | 是（缺了 configure 会失败） |
| MinGW g++ | 只有 `tools/menu_preview.cpp` 这个 PC 预览用 | 否 |

第一次：

```powershell
cd PicoBoy
.\build.ps1 -Clean
```

`-Clean` 是必要的：它会跑完整 configure，把 `gbc/roms/` 和 `nes/roms/` 打成
`build/gb_rom.h`、`build/nes_rom.h`。之后日常改代码用 `.\build.ps1` 就够了。

脚本开头会打印它认到的 `cmake` / `ninja` / `arm-none-eabi-gcc` 路径，
如果 SDK 装在了没被扫到的位置，设一个环境变量即可：

```powershell
$env:PICO_SDK_PATH = "D:\tools\pico-sdk"     # 指向 pico-sdk 本身
.\build.ps1 -Clean
```

**带走的两个烧录文件**在 `dist/`：`picoboy_gbc.uf2`、`pico_nes.uf2`。
重新构建后想刷新它们，跑 `.\build.ps1 -Dist`。

**改菜单**只需动 `bsp/src/pb_rom_menu.cpp`，两个固件一起变。

**加游戏**只要往 `gbc/roms/`（`.gb`/`.gbc`）或 `nes/roms/`（`.nes`）里丢文件，
再 `.\build.ps1 -Clean`。ROM 不方便外传的话可以自己筛，两个目录互不影响。
