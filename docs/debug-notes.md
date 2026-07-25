# ESP32-S3 I2S 音频调试笔记

## 环境

- 芯片：ESP32-S3 N16R8
- IDF 版本：v5.4.4
- 外设：INMP441 麦克风、MAX98357A 功放、0.96 寸 SSD1306 OLED
- 供电：USB（电脑或充电头）

---

## 问题 1：麦克风读数为 0，OLED 显示 `Raw: 0`

### 现象

`mic_read_sample()` 有返回（未阻塞），但采样值始终为 0。

### 原因

I2S 配置 `slot_mode = I2S_SLOT_MODE_MONO`，但 INMP441 按立体声 I2S 协议输出数据（每帧两个 slot，左/右声道交替）。MONO 模式下 ESP32 的 I2S 外设只认一个 slot，与实际数据帧格式不匹配，读取时抓到的始终是空 slot 或偏移位置的数据。

同时 `slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO` 和 `ws_width = I2S_SLOT_BIT_WIDTH_AUTO` 在 MONO 模式下生成的 WS 信号占空比可能不符合 INMP441 预期。

### 解决办法

```c
.slot_cfg = {
    .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
    .slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT,  // 显式 32bit
    .slot_mode      = I2S_SLOT_MODE_STEREO,       // 必须是 STEREO
    .slot_mask      = I2S_STD_SLOT_LEFT,           // L/R 接地 = 左声道
    .ws_width       = 16,                          // 显式 16，50% 占空比
    ...
},
```

---

## 问题 2：INMP441 VDD 电压偏低（万用表读数 2.5~2.9V）

### 现象

万用表测量 INMP441 模块 VDD 对 GND，读数在 2.5V~2.9V 之间波动，远低于 3.3V 标准值。但 OLED 能正常显示，芯片在跑。

### 真正原因

**万用表电池电量不足**，导致读数偏高/偏低不可信。实际 3.3V 供电正常，OLED 能亮就是最好的证据。

同时排查过程中发现电脑 USB 口供电确实偏弱，但这并非此问题的根因。

### 教训

> OLED 能亮 → 3.3V 没问题。不要轻易怀疑供电，先换万用表电池。

### 解决方法

1. 万用表换 9V 电池
2. 用一节 1.5V AA 电池验证万用表准确度
3. 测 ESP32 排针上的 3.3V 脚，正常应为 3.2~3.4V

---

## 问题 3：录音 malloc 失败，系统卡死

### 现象

```c
int16_t *buf = malloc(16000 * 8 * sizeof(int16_t));  // 8 秒 → 256KB
```

分配失败返回 NULL，程序卡死在录音阶段。

### 原因

ESP32-S3 内部 SRAM 可用堆约为 300~400KB。I2S DMA 缓冲区、OLED 帧缓冲、WiFi 协议栈等都要占用。8 秒 PCM 数据 = 256KB，剩余可用内存不够。

### 解决办法

- 短期：减少录音时长，2 秒（64KB）无压力
- 长期：开启 PSRAM

```
idf.py menuconfig
  → Component config
    → ESP PSRAM
      → [*] Support for external PSRAM
```

N16R8 模组有 8MB Octal PSRAM，开启后可用内存增加数十倍。

### 防御性代码

```c
int16_t *buf = malloc(N * sizeof(int16_t));
if (buf == NULL) {
    // 显示错误，不要继续执行
    ssd1306_draw_string(0, 0, "Malloc failed!");
    while (1) vTaskDelay(1000);
}
```

---

## 问题 4：扬声器播放卡死，`spk_write()` 不返回

### 现象

调用 `spk_write(buf, 256)` 后程序阻塞，OLED 不再更新，扬声器可能发出持续杂音。

### 原因

I2S DOUT 引脚使用了 **GPIO1**。该引脚在某些 ESP32-S3 模组上与内部功能冲突，导致 I2S TX 通道无法正常输出数据。DMA 缓冲区数据发不出去，`i2s_channel_write()` 以 `portMAX_DELAY` 阻塞等待，形成死锁。

### 解决方法

将 I2S DOUT 从 GPIO1 改为 **GPIO5**：

```c
// audio.h
#define I2S_DOUT  5   // 原来是 1
```

同时物理接线也要对应调整：MAX98357A 的 DIN 脚接到 GPIO5。

---

## 问题 5：麦克风-扬声器直通时声音模糊、刺耳

### 现象

将麦克风数据直接写入扬声器（直通/loopback），听到的声音完全无法辨认，伴随尖锐啸叫。

### 原因

**声学反馈（Howling）**：麦克风把扬声器发出的声音又收进去了，形成正反馈循环。

```
你说"你好" → 麦收到 → 喇叭放出 → 麦又收到 → 喇叭又放 → …（指数增长）
```

这跟 KTV 里话筒对着音箱的啸叫原理一样。

### 解决方法

1. **录播分离**（治本）：先录一段，录完再播放。播放期间麦克风不工作，彻底切断反馈路径。
2. **物理隔离**：麦克风和扬声器尽量拉远，背对背放置。
3. **降低增益**：播放数据除以 4~8 倍衰减。

---

## 问题 6：VSCode IntelliSense 误报 `left_align` 字段不存在

### 现象

```c
.slot_cfg = {
    ...
    .left_align = true,   // 红线：结构没有字段 "left_align"
},
```

### 原因

`i2s_std_slot_config_t` 结构体中 `left_align` 字段受 `#if SOC_I2S_HW_VERSION_1` 条件编译控制。ESP32-S3 走 `#else` 分支，字段确实存在。VSCode 的 IntelliSense 使用了错误的宏定义解析了 `#if` 分支，导致误报。

### 解决方法

- 忽略红线，`idf.py build` 编译一定过
- `Ctrl+Shift+P` → `C/C++: Rescan Workspace` 重新扫描

---

## 最终可用配置总结

### I2S 参数

| 参数 | 值 | 说明 |
|------|-----|------|
| `sample_rate` | 16000 | 语音场景足够 |
| `slot_mode` | `STEREO` | INMP441 只支持立体声协议 |
| `slot_bit_width` | `32BIT` | 显式指定 |
| `data_bit_width` | `16BIT` | 每采样 2 字节 |
| `ws_width` | `16` | 50% 占空比，显式指定 |
| `slot_mask` | `LEFT` | L/R 接地对应左声道 |
| `bit_shift` | `true` | Philips 标准：延迟 1 BCLK |
| `left_align` | `true` | 数据在 slot 中左对齐 |

### 引脚分配

```
麦克风 INMP441：VDD→3.3V  GND→GND  SCK→GPIO41  WS→GPIO42  SD→GPIO2  L/R→GND
功放 MAX98357A：VIN→3.3V  GND→GND  BCLK→GPIO41  LRC→GPIO42  DIN→GPIO5  SD→GPIO4
OLED SSD1306：  VCC→3.3V  GND→GND  SCL→GPIO20  SDA→GPIO21
```

### INMP441 注意事项

- **L/R 脚必须接 GND**，悬空会导致不输出数据
- 模块丝印 VDD 和 VCC 等价，只是不同厂家标法不同
- 使用 I2S 立体声协议，并非"单声道麦克风 = I2S MONO 模式"

---

## 问题 7：录制回放音质模糊、录音中断有杂音

### 现象

16kHz 采样率下录播循环，回放声音模糊不清。提升到 44.1kHz 后音质改善，但出现断续杂音（pop/click）。

### 原因

1. **16kHz 采样率对语音清晰度偏低**：奈奎斯特频率仅 8kHz，丢失大量高频细节。

2. **录制过程中刷新 OLED 导致 I2S RX 缓冲区溢出**：`ssd1306_update()` 通过 I2C 写入 ~1KB 数据耗时约 20ms。在 44.1kHz 下，20ms 积压 882 个采样点，DMA 缓冲区有限，溢出后丢数据，录音出现断点。

3. **播放过程中刷新 OLED 同样导致 TX 欠载**：44.1kHz 下每 256 采样点的 chunk 仅 5.8ms 就播完，OLED I2C 写入耗时 10-20ms 远超 chunk 间隔，下一个 chunk 来不及写入 DMA，产生音频缝隙。

### 解决方法

1. 日常语音场景使用 16kHz（电话音质够用），高保真场景使用 44.1kHz
2. **录制和播放期间都不要调用 `ssd1306_update()`**——只在录入前/播完后更新显示
3. 播放使用分块写入（CHUNK=256），每块间不做任何阻塞操作

```c
// 正确：录音/播放期间不碰 I2C
for (int i = 0; i < total; i++)
    buf[i] = mic_read_sample();    // 纯读，无中断

spk_write(buf, total);             // 纯写，无中断
```

---

## 问题 8：TTS 播放语速异常（2 倍速）

### 现象

通过 Python `edge-tts` 生成的 16kHz PCM 语音，在 ESP32 上播放时语速翻倍，即使 TTS 降速到 -50% 仍然偏快。

### 原因

I2S TX 配置为 `STEREO` 模式（与 RX 共享时钟），TX 外设期望的数据格式是**左右声道交替**：

```
写入: [L0, R0, L1, R1, L2, R2, ...]
```

但代码传入的是**单声道数据**：

```
实际写入: [D0, D1, D2, D3, D4, D5, ...]
I2S 解析:  L0=D0, R0=D1, L1=D2, R1=D3, ...
```

配合 `slot_mask = LEFT`，只有 L 声道被输出到引脚，R 声道被丢弃。于是**隔一个丢一个**——D0 播放，D1 丢弃，D2 播放，D3 丢弃……有效数据只剩一半，播放时长减半，速度翻倍。

### 解决方法

`spk_write()` 内部做**单声道→立体声交织**，每个采样点复制为 L/R 两份：

```c
void spk_write(const int16_t *samples, size_t count)
{
    int16_t stereo[512];
    for (size_t i = 0; i < count; i += 256) {
        size_t n = (i + 256 <= count) ? 256 : (count - i);
        for (size_t j = 0; j < n; j++) {
            stereo[j * 2]     = samples[i + j];  // L
            stereo[j * 2 + 1] = samples[i + j];  // R = same
        }
        i2s_channel_write(tx_handle, stereo, n * 2 * sizeof(int16_t),
                          &bytes_written, portMAX_DELAY);
    }
}
```

播放时长恢复正常，语速正确。

---

## 问题 9：纯音测试与录音回放音质对比

### 现象

ESP32 生成的正弦波音阶通过喇叭播放，声音清晰、无杂音。但同样的喇叭播放麦克风录音时，声音模糊。

### 原因

不是喇叭或功放的问题——正弦波清晰证明了 **MAX98357A + 喇叭的硬件链路完好**。模糊来自两个环节：

1. **INMP441 麦克风模块本身素质有限**：MEMS 麦克风的频率响应和信噪比不同于专业录音设备
2. **录音链路中的信号衰减/过载**：Peak 值过高（30000+/32767）说明信号接近满量程，可能出现削顶失真

### 结论

> 纯正弦波测试 = 验证功放和喇叭的硬件基准。这个通过了，硬件就没问题。
> 录音模糊 = 麦克风端或录音参数需要优化，不是功放/喇叭的锅。

### 优化方向

- 适当衰减录音信号（`/2` 或 `/4`）避免削顶
- 麦克风离嘴 10-15cm，不要太近
- 44.1kHz 比 16kHz 音质提升明显，但需注意缓冲区大小和 OLED 冲突
