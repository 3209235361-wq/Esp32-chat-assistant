#include <stdio.h>
#include <stdlib.h>
#include "audio.h"
#include "wifi.h"
#include "ssd1306.h"
#include "key.h"
#include "voice_client.h"
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

static int16_t *rec_buf  = NULL;   // 录音缓冲（malloc 到 PSRAM）
static int16_t *play_buf = NULL;   // 播放缓冲

QueueHandle_t rec_queue = NULL;
QueueHandle_t audio_queue = NULL;
TaskHandle_t oled_task = NULL;

//录音标志
typedef enum cmd{CMD_START_REC, CMD_STOP_REC} cmd_t;
//OLED Display
typedef enum str{
    Press = 0,
    Record = 1,
    Send = 2,
    Play = 3,
    Failed = 4,
    Empty = 5
} str_state;
char *str[6]={"Pressing key...","Recording...",
    "Sending to AI...","Playing reply...","Sending failed","Empty queue"};


void Task_Record(void *parameter){
    cmd_t cmd;
    size_t rec_len=0;
    while(1){
        xQueueReceive(rec_queue, &cmd, portMAX_DELAY);
        if(cmd!=CMD_START_REC){continue;}
        //cmd == CMD_START_REC 就发通知
        xTaskNotifyIndexed(oled_task,0,Record,eSetValueWithOverwrite);
        rec_len=0;        
        while(rec_len<MAX_SAMPLES){
            rec_buf[rec_len++]=mic_read();
            if(xQueueReceive(rec_queue, &cmd, 0)==pdTRUE&&cmd==CMD_STOP_REC){
                break;
            }
        }
        xQueueSend(audio_queue, &rec_len, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
void Task_Key(void *parameter){
    key_state_t status=KEY_NOT_PRESSED;
    bool last_status=false;
    cmd_t cmd;
    while(1){
        status=KEY_De_trembing(KEY_PIN);
        if(status==KEY_PRESSED&&!last_status){
            cmd=CMD_START_REC;
            xQueueSend(rec_queue, &cmd, portMAX_DELAY);
            xTaskNotifyIndexed(oled_task,0,Press,eSetValueWithOverwrite);
        }
        else if(status==KEY_NOT_PRESSED&&last_status){
            cmd=CMD_STOP_REC;
            xQueueSend(rec_queue, &cmd, portMAX_DELAY);
        }
        //边沿检测：以防重复发CMD_START_REC
        last_status=(status==KEY_PRESSED);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
void Task_Handle_Play(void *parameter){
    size_t rec_len=0;
    size_t play_len=0;
    while(1){
        if(xQueueReceive(audio_queue, &rec_len, portMAX_DELAY)==pdTRUE){
            xTaskNotifyIndexed(oled_task,0,Send,eSetValueWithOverwrite);
            play_len= RECV_MAX;
            bool ok=voice_send_receive(rec_buf, rec_len, play_buf, &play_len);
            if(ok==false||play_len==0){
                xTaskNotifyIndexed(oled_task,0,Failed,eSetValueWithOverwrite);
                continue;
            }    
        }
        amp_enable(true);
        xTaskNotifyIndexed(oled_task,0,Play,eSetValueWithOverwrite);
        //播放容量改为最大，确保播放完整（ai回复大小一般会大于录音大小）
        spk_write(play_buf, play_len);
        vTaskDelay(pdMS_TO_TICKS(300));
        amp_enable(false);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void Task_OLED_Display(void *parameter){
    uint32_t state=Empty;
    while(1){
        xTaskNotifyWaitIndexed(0,0,0,&state,portMAX_DELAY);//for recording
        ssd1306_clear_row(24);
        ssd1306_draw_string(0,24,str[state]);
        ssd1306_update();        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void)
{
    KEY_Init();
    // ---- 1. OLED 初始化 ----
    ssd1306_init(I2C_SDA_PIN, I2C_SCL_PIN);
    ssd1306_draw_string(0, 0, "Booting...");
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

    rec_queue=xQueueCreate(1, sizeof(cmd_t));
    audio_queue=xQueueCreate(1, sizeof(size_t));
    xTaskCreate(Task_Record, "Rec", 2048, NULL, 4, NULL);
    xTaskCreate(Task_Key, "Key", 2048, NULL, 3, NULL);
    xTaskCreate(Task_Handle_Play, "Play", 8192, NULL, 2, NULL);
    xTaskCreate(Task_OLED_Display, "OLED", 2048, NULL, 1, &oled_task);


    while (1){
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
