#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "audio.h"
#include "wifi.h"
#include "ssd1306.h"
#include "key.h"
#include "led.h"
#include "Monitor.h"
#include "voice_client.h"
#include "wakeup.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// ---- 改这里 ----
#define WIFI_SSID      "宇智波皮的iPhone"
#define WIFI_PASSWORD  "77777777"

#define SERVER_IP      "172.20.10.3"   // 手机热点：172.20.10.x，连同一热点后查电脑IP
#define SERVER_PORT    8006

#define I2C_SCL_PIN    20
#define I2C_SDA_PIN    21

#define RECORD_SEC     30                // 假设最大录音秒数
#define MAX_SAMPLES    (SAMPLE_RATE * RECORD_SEC)      // 最大录音缓冲大小
#define RECV_MAX       (SAMPLE_RATE * 20)              // 接收缓冲最大 20 秒
#define SILENCE_END_FRAMES 16

static int16_t *rec_buf=NULL;   // 录音缓冲（malloc 到 PSRAM）
static int16_t *play_buf = NULL;   // 播放缓冲

static char command[16];

QueueHandle_t audio_queue = NULL;
QueueHandle_t command_queue = NULL;
TaskHandle_t oled_task = NULL;
SemaphoreHandle_t for_record = NULL;

//录音标志
typedef enum cmd{CMD_START_REC, CMD_STOP_REC} cmd_t;

//OLED Display
char *oled_state[6]={"Waiting wake word...","Recording...",
    "Sending to AI...","Playing reply...","Sending failed","Empty queue"};
char *oled_command[8]={"led_on","led_off","led_high","led_middle",
    "motor_on","motor_off","motor_high","motor_middle"};

//for wakeup
extern i2s_chan_handle_t rx_handle;
extern esp_afe_sr_data_t *afe_data;
extern const esp_afe_sr_iface_t *afe_handle;
extern const char *TAG0;
static int afe_mic_read(int16_t *rec_buf,int samples){
    size_t rec_len=0;   
    esp_err_t err=i2s_channel_read(rx_handle,rec_buf,samples*2,&rec_len,portMAX_DELAY);
    return (err==ESP_OK)?(int)rec_len:-1;//返回字节数
}

void Task_AFE_Mic(void *parameter){
    //wakeword=你好小智
    int feed_chunk=afe_handle->get_feed_chunksize(afe_data);
    int16_t *in_buf=heap_caps_malloc(feed_chunk*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    assert(in_buf);

    static int silence_frames=0;
    bool woken=false;
    int offset=0;
    while(1){
        vTaskDelay(pdMS_TO_TICKS(10));

        //读取麦克风数据,feed给afe_data
        if(afe_mic_read(in_buf,feed_chunk)!=feed_chunk*2){
            continue;
        }
        afe_handle->feed(afe_data,in_buf);

        //获取数据
        afe_fetch_result_t *res=afe_handle->fetch(afe_data);
        
        //唤醒检测
        if(!woken&&res->wakeup_state==WAKENET_DETECTED){
            woken=true;
            silence_frames=0;
            offset=0;
            ESP_LOGI(TAG0,"wake word! index=%d",res->wake_word_index);
        }

        //录音处理
        if(woken){
            xTaskNotifyIndexed(oled_task,0,Record,eSetValueWithOverwrite);
            //判断是否超过最大录音缓冲大小
            if(offset+res->data_size>=MAX_SAMPLES*2){
                ESP_LOGI(TAG0,"speech end");
                int samples=offset/2;
                xQueueSend(audio_queue, &samples, portMAX_DELAY);
                offset=0;
                silence_frames=0;
                woken=false;                
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            //将数据复制到rec_buf
            xSemaphoreTake(for_record,portMAX_DELAY);
            memcpy((uint8_t *)rec_buf+offset,res->data,res->data_size);
            xSemaphoreGive(for_record);
            offset+=res->data_size;

            //静音检测
            if(res->vad_state==VAD_SILENCE){
                if(++silence_frames>=SILENCE_END_FRAMES){
                    ESP_LOGI(TAG0,"speech end");
                    woken=false;
                    int samples=offset/2;
                    xQueueSend(audio_queue, &samples, portMAX_DELAY);
                    offset=0;
                    silence_frames=0;
                    vTaskDelay(pdMS_TO_TICKS(100));
                } 
            }
            else{silence_frames=0;}
        }
    }
}
void Task_Handle_Play(void *parameter){
    size_t samples=0;
    size_t play_len=0;
    while(1){
        if(xQueueReceive(audio_queue, &samples, portMAX_DELAY)==pdTRUE){
            xTaskNotifyIndexed(oled_task,0,Send,eSetValueWithOverwrite);
            play_len= RECV_MAX;
            xSemaphoreTake(for_record , portMAX_DELAY);
            bool ok=voice_send_receive(rec_buf, samples, play_buf, &play_len);
            xSemaphoreGive(for_record);
            if(ok==false||play_len==0){
                xTaskNotifyIndexed(oled_task,0,Failed,eSetValueWithOverwrite);
                continue;
            }

            //播放回复
            amp_enable(true);
            xTaskNotifyIndexed(oled_task,0,Play,eSetValueWithOverwrite);
            //播放容量改为最大，确保播放完整（ai回复大小一般会大于录音大小）
            spk_write(play_buf, play_len);
            vTaskDelay(pdMS_TO_TICKS(300));
            amp_enable(false);
            vTaskDelay(pdMS_TO_TICKS(100));

            strncpy(command,voice_last_command(),sizeof(command)-1);
            command[sizeof(command)-1]='\0';            
            xQueueSend(command_queue, command , portMAX_DELAY);
        }
        
    }
}


void Task_Command(void *parameter){
    static char temp_command[16];
    while(1){
        if(xQueueReceive(command_queue, temp_command, portMAX_DELAY)==pdTRUE){
            if(strcmp(temp_command,"led_on")==0){
                Set_Level_LED(LED_DEFAULT_BRIGHTNESS);
                xTaskNotifyIndexed(oled_task,1,LED_ON,eSetValueWithOverwrite);
            }
            else if(strcmp(temp_command,"led_off")==0){
                Set_Level_LED(LED_CLOSE);
                xTaskNotifyIndexed(oled_task,1,LED_OFF,eSetValueWithOverwrite);
            }
            else if(strcmp(temp_command,"motor_on")==0){
                motor_set_speed(DEFAULT_SPEED);
                xTaskNotifyIndexed(oled_task,1,MOTOR_ON,eSetValueWithOverwrite);
            }
            else if(strcmp(temp_command,"motor_off")==0){
                motor_set_speed(MOTOR_CLOSE);
                xTaskNotifyIndexed(oled_task,1,MOTOR_OFF,eSetValueWithOverwrite);
            }
            else if(strcmp(temp_command,"led_high")==0){
                Set_Level_LED(LED_MAX_BRIGHTNESS);
                xTaskNotifyIndexed(oled_task,1,LED_HIGH,eSetValueWithOverwrite);
            }
            else if(strcmp(temp_command,"led_middle")==0){
                Set_Level_LED(LED_MIDDLE_BRIGHTNESS);
                xTaskNotifyIndexed(oled_task,1,LED_MIDDLE,eSetValueWithOverwrite);
            }
            else if(strcmp(temp_command,"motor_high")==0){
                motor_set_speed(MAX_SPEED);
                xTaskNotifyIndexed(oled_task,1,MOTOR_HIGH,eSetValueWithOverwrite);
            }
            else if(strcmp(temp_command,"motor_middle")==0){
                motor_set_speed(MIDDLE_SPEED);
                xTaskNotifyIndexed(oled_task,1,MOTOR_MIDDLE,eSetValueWithOverwrite);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void Task_OLED_Display(void *parameter){
    uint32_t state=Empty;
    uint32_t command=LED_ON;
    while(1){
        xTaskNotifyWaitIndexed(0,0,0,&state,pdMS_TO_TICKS(100));
        xTaskNotifyWaitIndexed(1,0,0,&command,pdMS_TO_TICKS(100));
        ssd1306_clear_row(32);
        ssd1306_draw_string(0,32,oled_state[state]);
        ssd1306_update();  
        ssd1306_clear_row(48);
        ssd1306_draw_string(0,48,oled_command[command]);
        ssd1306_update();  
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void)
{
    KEY_Init();
    LED_Init();
    Monitor_Init();
    WakeEngine_Init();
    
    // ---- 1. OLED 初始化 ----
    ssd1306_init(I2C_SDA_PIN, I2C_SCL_PIN);
    ssd1306_draw_string(0, 0, "Chat Assistant");
    ssd1306_update();

    ssd1306_draw_string(0,24,"chat   state:");
    ssd1306_update();
    ssd1306_draw_string(0,40,"device state:");
    ssd1306_update();

    // ---- 2. 音频初始化 ----
    Audio_Init();
    amp_enable(false);  // 先静音

    // ---- 3. 连接 WiFi ----
    ssd1306_draw_string(0, 16, "WiFi...");
    ssd1306_update();
    WiFi_Connect(WIFI_SSID, WIFI_PASSWORD);
    ssd1306_draw_string(0, 16, "WiFi OK!       ");
    ssd1306_update();

    // ---- 4. 设后端地址 + 分配缓冲区 ----
    voice_set_server(SERVER_IP, SERVER_PORT);
    rec_buf  = malloc(MAX_SAMPLES * sizeof(int16_t));
    play_buf = malloc(RECV_MAX   * sizeof(int16_t));
    if (!rec_buf || !play_buf) {
        ssd1306_draw_string(0, 32, "Malloc failed!");
        ssd1306_update();
        while (1) vTaskDelay(1000);
    }
    printf("[Init] rec=%d samples  play=%d samples\n", MAX_SAMPLES, RECV_MAX);

    audio_queue=xQueueCreate(1, sizeof(size_t));
    command_queue=xQueueCreate(1, sizeof(command));
    for_record=xSemaphoreCreateMutex();

    xTaskCreate(Task_AFE_Mic, "Mic", 8192, NULL, 4, NULL);
    xTaskCreate(Task_Handle_Play, "Play", 8192, NULL, 2, NULL);
    xTaskCreate(Task_OLED_Display, "OLED", 2048, NULL, 1, &oled_task);
    xTaskCreate(Task_Command, "Command", 2048, NULL, 1, NULL);

    while (1){   
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
