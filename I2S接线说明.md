# PicoBoy I2S 音频接线说明（MAX98357A）

I2S 是数字总线，必须外接一颗 I2S DAC 模块。推荐 **MAX98357A**：自带 D 类功放可直接推喇叭、无需 MCLK、便宜。

## 接线（3 根信号线 + 电源）

| MAX98357A 引脚 | 接到 RP2040 | 说明 |
|---|---|---|
| VIN | 3V3 | 供电（用 3.3V，逻辑电平一致） |
| GND | GND | 地 |
| BCLK | GPIO14 | 位时钟 |
| LRC | GPIO15 | 左右声道选择（WS） |
| DIN | GPIO13 | 数据（注意：是数据，不是 shutdown） |
| GAIN | 悬空 | 增益，默认 9dB；接 GND=12dB、接 VIN=3dB |
| SD（shutdown） | 悬空 | 关机脚，悬空=正常工作 |

喇叭接模块上的 **+ / −** 输出（4Ω 或 8Ω 均可）。

```
RP2040                         MAX98357A
3V3  ------------------------ VIN
GND  ------------------------ GND
GP13 (DATA)   --------------- DIN
GP14 (BCLK)   --------------- BCLK
GP15 (LRCLK)  --------------- LRC
                             +---- 喇叭 +
                             - ---- 喇叭 -
```

## 编译

默认仍是 PWM（单 GPIO）。要出 I2S 固件，configure 时加 `-DPB_AUDIO_I2S=1`：

```
cmake -B build -DPB_AUDIO_I2S=1
ninja -C build
```

两种几何（240×240、320×240 横屏）都已验证可编译。预编译 I2S 固件在 `dist/i2s_240x240` 与 `dist/i2s_320x240_landscape`。

## 与 PWM 的区别

- I2S 走数字信号，D/A 转换在模块内完成，16bit、信噪比高、无 PWM 载波声和 RC 滤波残留，背景更干净。
- I2S 环形缓冲为 8KB（PWM 为 4KB），实测 bss 各 +4096，仍在 264KB RAM 余量内。
- 引脚 11（PWM）在 I2S 模式下不再使用。
