# roms/ — 放 .nes 文件的地方

把 `*.nes` 丢进这个目录，然后直接构建：

```powershell
.\build.ps1
```

`tools/gen_nes_rom.py` 会在 cmake configure 时扫描这里，把每份 ROM 编成
`nes_rom.h` 里的 const 数组（放在 flash 里，不占 RAM）。**文件名就是菜单里的显示名**，
所以要用 ASCII（板子的 3×5 点阵只认 `0-9 A-Z . : - / _ ? ! +`，中文会被打成空格）。

菜单按文件名字母序排列（14 行一屏，上下滚动，底部显示 `MAPPER n PRG xK CHR xK`）。

## 目录里现在有什么

| 菜单名 | 原文件名 | 游戏 | mapper | PRG / CHR | 大小 |
|---|---|---|---|---|---|
| `CONTRA` | 2.魂斗罗1代 无限人+散弹枪.nes | 魂斗罗（无限人+散弹） | 23 | 128K / 128K | 256 KB |
| `DRAGON` | 75.龙魂无敌版.nes | 龙魂（无敌） | 88 | 128K / 128K | 256 KB |
| `GIJOE` | 47.G.I.J特殊部队HACK.nes | G.I.JOE 特种部队 | 4 | 128K / 256K | 384 KB |
| `GUEVARA` | 1.古巴战士无闪烁不死ＨＡＣＫ.nes | 古巴战士（不死+无闪烁） | 1 | 128K / 128K | 256 KB |
| `JACKAL` | 139.赤色要塞无敌版.nes | 赤色要塞（无敌） | 2 | 128K / CHR RAM | 128 KB |
| `LIFEFORCE` | 52.沙罗曼蛇2加强版.nes | 沙罗曼蛇 2（加强） | 73 | 128K / CHR RAM | 128 KB |
| `RUSHNA` | 76.绿色兵团无限人.nes | 绿色兵团（无限人） | 2 | 128K / CHR RAM | 128 KB |
| `SAIYUKI2` | 120.西游记2无敌.nes | 西游记 2（无敌） | 18 | 128K / 128K | 256 KB |
| `PB_NES_TEST` | （本工程生成） | 自检图案（白/红/绿/蓝四象限） | 0 | 16K / 8K | 24 KB |

合计 **1816 KiB**，UF2 约 3.9 MB（16 MB flash 的 1/4）。

## PC 侧实测（`tools\hosttest\host_test.cpp`，不是推断）

同一套核心、同一套零拷贝 ROM 解析，在 PC 上跑 240 帧（自动按 Start）：

| 菜单名 | 画面颜色数 | 结论 |
|---|---|---|
| CONTRA | 13 | 标题画面正常 |
| GIJOE | 9 | 标题 + MISSION 1 简报正常 |
| JACKAL | 12 | 标题画面正常 |
| LIFEFORCE | 6 | 启动画面正常 |
| RUSHNA | 12 | 标题（星空/雪山/直升机）正常 |
| SAIYUKI2 | 12 | 标题场景正常 |
| GUEVARA | 11（需约 900 帧） | 前 4 秒是白屏过场，之后进丛林场景，正常 |
| DRAGON | 5（需约 900 帧） | 前 4 秒是黑屏过场，之后出日文剧情画面，正常 |
| PB_NES_TEST | 4 | 白/红/绿/蓝 四象限 |

复现：

```powershell
build\host_test.exe roms\CONTRA.nes build\shot.ppm 240     # 第 5 个参数 0 = 不自动按 Start
```

> 这 8 个都是"无敌版 / HACK"版，iNES 头里的 mapper 有些是改过的
> （真的魂斗罗是 mapper 2，这里写 23；真的龙魂是 mapper 4/206，这里写 88）。
> InfoNES 认这个头，PC 侧跑出来是对的；如果真机上某个游戏花屏/死机，
> 换一个未改头的版本再试是第一步。

## 关于自检 ROM

`PB_NES_TEST` 由 `tools/make_test_rom.py` 生成，不是游戏，不涉及任何版权素材。
它把整条链路串了一遍：CPU 跑代码 → `$2006/$2007` 写调色板和属性表 → PPU 取图块
→ 属性选调色板 → 逐行输出 → 裁边 → RGB565。**烧板后先选它**：看到彩色棋盘格
就说明板子、BSP、推屏全通；纯白/灰/花屏则参考 `README.md` 第 9 节。

不想要它就删掉再构建（`roms/` 空了也照样能编译，上电显示 `NO ROM`）。

## 支持的 mapper

保留 133 个，名单见 `README.md` 第 8 节。**mapper 5 / 6 / 21 / 85 / 235 已摘掉**
（RAM 不够），遇到会显示 `ROM ERROR`。

请只使用你自己合法拥有的 ROM。
