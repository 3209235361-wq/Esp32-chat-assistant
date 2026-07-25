#include "audio_temp.h"
#include "driver/i2s_std.h"   // I2S 标准模式驱动（Philips 格式）
#include "driver/gpio.h"      // GPIO 操作（功放使能脚）
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ---- 句柄：每个句柄代表一个 I2S 通道，后续所有操作都靠它定位 ----
static i2s_chan_handle_t rx_handle;  // RX 通道句柄：麦克风 → ESP32
static i2s_chan_handle_t tx_handle;  // TX 通道句柄：ESP32 → 功放

// ================================================================
//  audio_init() — 初始化 I2S 全双工（麦克风收 + 功放发 共用时钟）
// ================================================================
void audio_init(void)
{
    // ===== 第1步：关功放（避免 I2S 初始化时引脚抖动产生爆音）=====
    gpio_reset_pin(AMP_SD);                       // GPIO4 恢复默认
    gpio_set_direction(AMP_SD, GPIO_MODE_OUTPUT);  // 设为输出模式
    gpio_set_level(AMP_SD, 0);                     // 拉低 = 功放关断、静音

    // ===== 第2步：创建 I2S 通道（全双工：一收一发）=====
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    //                           └──────┬──────┘          └───────┬───────┘
    //                        I2S_NUM_0: 用0号I2S外设   I2S_ROLE_MASTER: ESP32做主设备
    //                                                (ESP32主动输出BCLK和WS时钟)
    i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle);
    //            传入配置      TX句柄      RX句柄
    //            (驱动填好)   (ESP32→功放) (麦克风→ESP32)

    // ===== 第3步：填配置结构体（时钟 + 数据格式 + 引脚）=====
    i2s_std_config_t std_cfg = {
        // --- 3a. 时钟 ---
        // BCLK = SAMPLE_RATE × slot_bit_width × 2个声道 = 16000×32×2 = 1.024 MHz
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),

        // --- 3b. 数据在 I2S 帧里的摆放方式 ---
        .slot_cfg = {
            // 每个采样点的精度：16位 = 音量分成65536级（CD音质级别）
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            // 每个声道在总线上占32个BCLK时钟周期（即使数据只用了16位）
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT,
            // 立体声模式：WS=高时传左声道，WS=低时传右声道
            // INMP441只支持立体声协议，即使用MONO也会出问题
            .slot_mode      = I2S_SLOT_MODE_STEREO,
            // 只取左声道（INMP441的L/R脚接地 = 左声道模式）
            .slot_mask      = I2S_STD_SLOT_LEFT,
            // WS高电平占16个BCLK，低电平也占16个 → 50%占空比
            .ws_width       = 16,
            // WS极性：false=WS高是左声道，WS低是右声道
            .ws_pol         = false,
            // Philips I2S标准：WS翻转后延迟1个BCLK才开始传数据
            .bit_shift      = true,
            // 16位数据在32位slot里靠左对齐（高位在前，后面16位空着）
            .left_align     = true,
            // 小端字节序（ESP32是little-endian）
            .big_endian     = false,
            // 每个字节内高位(MSB)先发
            .bit_order_lsb  = false,
        },

        // --- 3c. 把引脚分配给 I2S 外设 ---
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,   // 主时钟不用（INMP441和MAX98357A都不需要）
            .bclk = I2S_BCLK,          // 位时钟 → GPIO41（麦克风和功放共用同一根）
            .ws   = I2S_LRCK,          // 左右时钟 → GPIO42（同上，共用）
            .dout = I2S_DOUT,          // 数据输出 → GPIO5（ESP32发给功放）
            .din  = I2S_DIN,           // 数据输入 → GPIO2（麦克风发给ESP32）
            .invert_flags = {          // 三个时钟信号都不反转
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    // 把上面的配置同时写入 TX 通道和 RX 通道（共用同一套时钟和格式）
    i2s_channel_init_std_mode(tx_handle, &std_cfg);
    i2s_channel_init_std_mode(rx_handle, &std_cfg);

    // ===== 第4步：启用通道（从这行开始 BCLK 和 WS 时钟持续输出）=====
    i2s_channel_enable(tx_handle);
    i2s_channel_enable(rx_handle);
    // 注意：此时功放还是关的，等调用 amp_enable(true) 喇叭才会响
}

// ================================================================
//  mic_read_sample() — 从麦克风读一个采样点（阻塞等待）
//  返回：int16_t，范围 -32768~32767，0=静音
// ================================================================
int16_t mic_read_sample(void)
{
    int16_t sample = 0;                             // 存放一个采样点（2字节）
    size_t bytes_read = 0;                          // 实际读到的字节数（输出参数）
    i2s_channel_read(rx_handle,                    // 用RX句柄收麦克风数据
                     &sample,                       // 数据存到这里
                     sizeof(sample),                // 每次读2字节（= 1个16位采样）
                     &bytes_read,                   // 返回实际读了多少
                     portMAX_DELAY);                // 没数据就永远等（阻塞）
    return sample;
}

// ================================================================
//  spk_write() — 把 PCM 数据发给功放播放
//  内部自动做 单声道→立体声交织（因为 TX 是 STEREO 模式）
//  输入：samples = 单声道PCM数组，count = 采样点个数
// ================================================================
void spk_write(const int16_t *samples, size_t count)
{
    // I2S STEREO 模式要求数据格式：L0,R0, L1,R1, L2,R2, ...
    // 但我们的PCM音频只有单声道：[D0, D1, D2, ...]
    // 所以要把每个采样点复制一份，同时填进L和R位置
    // 不这样做的话，隔一个丢一个 → 2倍速
    int16_t stereo[512];                            // 栈上临时缓冲（512×2=1024字节，安全）
    size_t bytes_written = 0;

    for (size_t i = 0; i < count; i += 256) {       // 每次处理256个采样一批
        size_t n = (i + 256 <= count) ? 256 : (count - i);  // 最后一批可能不足256
        for (size_t j = 0; j < n; j++) {
            stereo[j * 2]     = samples[i + j];     // 左声道 = 原始数据
            stereo[j * 2 + 1] = samples[i + j];     // 右声道 = 同一份数据
        }
        // 写入长度要×2，因为一份数据变成了L+R两份
        i2s_channel_write(tx_handle,                // 用TX句柄发给功放
                          stereo,                    // 交织后的数据
                          n * 2 * sizeof(int16_t),  // 字节数 = 采样数×2声道×2字节
                          &bytes_written,
                          portMAX_DELAY);            // DMA忙就等，直到写完
    }
}

// ================================================================
//  amp_enable() — 控制 MAX98357A 功放的 SD 脚（Shutdown）
//  true=GPIO4高电平=功放工作, false=GPIO4低电平=功放关断静音
// ================================================================
void amp_enable(bool on)
{
    gpio_set_level(AMP_SD, on ? 1 : 0);             // 不需要等稳定，直接拉高/低
}
