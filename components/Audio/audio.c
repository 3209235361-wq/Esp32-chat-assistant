#include "audio.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ---- 句柄：操作 I2S 通道的"编号" ----
static i2s_chan_handle_t rx_handle;  // RX 通道，麦克风→ESP32
static i2s_chan_handle_t tx_handle;  // TX 通道，ESP32→功放


void Audio_Init(void)
{
    // ========== 第1步：关功放，防止初始化时喇叭爆音 ==========
    gpio_reset_pin(AMP_SD);
    gpio_set_direction(AMP_SD, GPIO_MODE_OUTPUT);
    gpio_set_level(AMP_SD, 0);                // 低电平 = 功放关断

    // ========== 第2步：初始化I2S通道 ==========
    i2s_chan_config_t chan_cfg= I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);
    // I2S_NUM_0: 主I2S通道，用于麦克风和喇叭
    // I2S_ROLE_MASTER: 主角色ESP32，用于输出BCLK和WS(clock data)
    i2s_new_channel(&chan_cfg,&tx_handle,&rx_handle);

    // ========== 第3步：填配置结构体 ==========
    i2s_std_config_t std_cfg = {
        .clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),

        .gpio_cfg={
            .bclk=I2S_BCLK,
            .ws=I2S_LRCK,
            .din=INMP441_SD,
            .dout=DIN,
            .mclk=I2S_GPIO_UNUSED,// 主时钟引脚未使用
            .invert_flags={// 不需要反转
                .bclk_inv=false,
                .ws_inv=false,
                .mclk_inv=false
            }
        },

        .slot_cfg={
            .big_endian=false,//esp是小端模式
            .bit_order_lsb=false,//MSB先输出
            .bit_shift=true,//飞利浦格式
            .data_bit_width=I2S_DATA_BIT_WIDTH_16BIT,//采样精度16位数据宽度
            .left_align=true,//左对齐，数据在高十六位，低十六位为空
            .slot_bit_width=I2S_SLOT_BIT_WIDTH_32BIT,//32位时钟周期
            .slot_mask=I2S_STD_SLOT_LEFT,// 只使用左声道
            .slot_mode=I2S_SLOT_MODE_STEREO,// 双声模式
            .ws_pol=false,// ws高电平左声道，ws低电平右声道
            .ws_width=16,// ws宽度为16个BCLK
        }
    };
    //先初始化TX通道，再初始化RX通道
    // 因为TX通道是主角色，用于输出BCLK和WS(clock data)，不然时序会乱,声音会失真
    i2s_channel_init_std_mode(tx_handle,&std_cfg);
    i2s_channel_init_std_mode(rx_handle,&std_cfg);

    // ========== 第4步：启用通道 ==========
    i2s_channel_enable(rx_handle);
    i2s_channel_enable(tx_handle);
}

// 读取麦克风数据
int16_t mic_read(void){
    int16_t sample=0;
    size_t bytes=0;
    i2s_channel_read(rx_handle,&sample,sizeof(sample),&bytes,portMAX_DELAY);
    return sample;
}

// 写入喇叭数据
// data: 声达数据指针
// count: 数据样本数（例如：sample rate=16000->count=16000，即录音一秒）
// 每个样本16位有符号整数
void spk_write(const int16_t *sample_data,size_t count){
    //i2s stereo的数据格式是：l0 r0 l1 r1 ...
    //因为只写左声道，所以要而外填充右声道数据
    size_t written_bytes=0;
    int16_t stereo[512];//buffer for stereo data

    for(size_t i=0;i<count;i+=256){//One round handles 256 datas.
        size_t n=(i+256<=count)?256:(count-i);
        for(int j=0;j<n;j++){
            stereo[j*2]=sample_data[i+j];
            stereo[j*2+1]=sample_data[i+j];
        }
        i2s_channel_write(tx_handle,stereo,sizeof(int16_t)*n*2,&written_bytes,portMAX_DELAY);
    }

}

//控制功放
void amp_enable(bool on){
    gpio_set_level(AMP_SD, on ? 1 : 0);             // 不需要等稳定，直接拉高/低
}