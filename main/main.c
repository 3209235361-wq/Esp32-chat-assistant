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
#include "device_manager.h"
#include "voice_client.h"
#include "wakeup.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "my_check.h"

// ---- change here/改这里 ----
#define WIFI_SSID      "宇智波皮的iPhone"
#define WIFI_PASSWORD  "77777777"

#define SERVER_IP      "172.20.10.3"   // phone hotspot/手机热点：172.20.10.x，连同一热点后查电脑IP
#define SERVER_PORT    8006

static int16_t *rec_buf[2]={NULL,NULL};   // 录音双缓冲（malloc 到 PSRAM）
static int rec_buf_index=0;   // 录音缓冲索引
struct msg{
    int rec_buf_index;
    int samples;
};
static int16_t *play_buf = NULL;   // 播放缓冲
const char *TAG_PLAY = "play";

static char command[16];

QueueHandle_t audio_queue = NULL;
QueueHandle_t key_queue = NULL;
QueueHandle_t command_queue = NULL;
SemaphoreHandle_t for_command = NULL;
SemaphoreHandle_t buf_free[2]={NULL,NULL};//for mic and play

static Header_t header;
static Node_t node1={.name="motor",.label="1.Motor state",.state=MOTOR_CLOSE_T,.state_index=motor_state,.state_table=motor_str_state,.list={NULL,NULL},.func=motor_adjust};
static Node_t node2={.name="led",.label="2.LED state",.state=LED_CLOSE_T,.state_index=led_state,.state_table=led_str_state,.list={NULL,NULL},.func=led_adjust};
static Node_t node3={.name="volume",.label="3.Adjust volume",.state=VOLUME_DEFAULT_T,.state_index=volume_state,.state_table=volume_str_state,.list={NULL,NULL},.func=volume_adjust};
static Node_t node4={.name="voice",.label="4.Switch voice",.state=STD_CHINESE_FM_MODEL,.state_index=voice_state,.state_table=voice_str_state,.list={NULL,NULL},.func=voice_adjust};

#define REFRESH_VALUE 0xFF
void wifi_change_to(void);
static void dump_stack_usage(void);


//for wakeup
extern i2s_chan_handle_t rx_handle;
extern esp_afe_sr_data_t *afe_data;
extern const esp_afe_sr_iface_t *afe_handle;
static int afe_mic_read(int16_t *rec_buf,int samples){
    size_t rec_len=0;   
    esp_err_t err=i2s_channel_read(rx_handle,rec_buf,samples*2,&rec_len,portMAX_DELAY);
    return (err==ESP_OK)?(int)rec_len:-1;//返回字节数
}

void Task_AFE_Mic(void *parameter){
    //wakeword=你好小智
    int feed_chunk=afe_handle->get_feed_chunksize(afe_data);
    int16_t *in_buf=heap_caps_malloc(feed_chunk*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(in_buf == NULL){
        ESP_LOGE(TAG_WAKEUP,"Task_AFE_Mic:in_buf malloc failed,inspect heap memory!");
        while(1) vTaskDelay(pdMS_TO_TICKS(100));
    }
    struct msg msg={0,0};
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
            xSemaphoreTake(buf_free[rec_buf_index],portMAX_DELAY);
            woken=true;
            silence_frames=0;
            offset=0;
            ESP_LOGI(TAG_WAKEUP,"wake word! index=%d",res->wake_word_index);
        }

        
        //录音处理
        if(woken){
            //判断是否超过最大录音缓冲大小
            if(offset+res->data_size>=MAX_SAMPLES*2){
                ESP_LOGI(TAG_WAKEUP,"speech end");
                int samples=offset/2;
                msg.rec_buf_index=rec_buf_index;
                msg.samples=samples;
                rec_buf_index=1-rec_buf_index;
                xQueueSend(audio_queue, &msg, portMAX_DELAY);
                offset=0;
                silence_frames=0;
                woken=false;                
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            //将数据复制到rec_buf
            memcpy((uint8_t *)rec_buf[rec_buf_index]+offset,res->data,res->data_size);    
            offset+=res->data_size;

            //静音检测
            if(res->vad_state==VAD_SILENCE){
                if(++silence_frames>=SILENCE_END_FRAMES){
                    ESP_LOGI(TAG_WAKEUP,"speech end");
                    // ESP_LOGI(TAG_WAKEUP,"unused stack:%u bytes",(unsigned)uxTaskGetStackHighWaterMark(NULL));
                    woken=false;
                    int samples=offset/2;
                    msg.rec_buf_index=rec_buf_index;
                    msg.samples=samples;
                    rec_buf_index=1-rec_buf_index;
                    xQueueSend(audio_queue, &msg, portMAX_DELAY);
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
    struct msg msg={0,0};
    size_t play_len=0;
    while(1){
        if(xQueueReceive(audio_queue, &msg, portMAX_DELAY)==pdTRUE){
            play_len= RECV_MAX;
            bool ok=voice_send_receive(rec_buf[msg.rec_buf_index], msg.samples, play_buf, &play_len);
            if(ok==false||play_len==0){
                ESP_LOGE(TAG_PLAY,"voice_send_receive failed!");
                xSemaphoreGive(buf_free[msg.rec_buf_index]);
                continue;
            }
            xSemaphoreGive(buf_free[msg.rec_buf_index]);
            //播放回复
            amp_enable(true);
            //播放容量改为最大，确保播放完整（ai回复大小一般会大于录音大小）
            spk_write(play_buf, play_len);
            vTaskDelay(pdMS_TO_TICKS(300));
            amp_enable(false);
            vTaskDelay(pdMS_TO_TICKS(100));

            // ESP_LOGI("Handle_Play","unused stack:%u bytes",(unsigned)uxTaskGetStackHighWaterMark(NULL));

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
            xSemaphoreTake(for_command,portMAX_DELAY);
            if(strcmp(temp_command,"led_on")==0){
                Set_Level_LED(LED_DEFAULT_BRIGHTNESS);
                node2.state=LED_DEFAULT_T; 
            }
            else if(strcmp(temp_command,"led_off")==0){
                Set_Level_LED(LED_CLOSE);
                node2.state=LED_CLOSE_T; 
            }
            else if(strcmp(temp_command,"motor_on")==0){
                motor_set_speed(DEFAULT_SPEED);
                node1.state=DEFAULT_SPEED_T; 
            }
            else if(strcmp(temp_command,"motor_off")==0){
                motor_set_speed(MOTOR_CLOSE);
                node1.state=MOTOR_CLOSE_T;
            }
            else if(strcmp(temp_command,"led_high")==0){
                Set_Level_LED(LED_MAX_BRIGHTNESS);
                node2.state=LED_MAX_T; 
            }
            else if(strcmp(temp_command,"led_middle")==0){
                Set_Level_LED(LED_MIDDLE_BRIGHTNESS);
                node2.state=LED_MIDDLE_T; 
            }
            else if(strcmp(temp_command,"motor_high")==0){
                motor_set_speed(MAX_SPEED);
                node1.state=MAX_SPEED_T; 
            }
            else if(strcmp(temp_command,"motor_middle")==0){
                motor_set_speed(MIDDLE_SPEED);
                node1.state=MIDDLE_SPEED_T; 
            }
            xSemaphoreGive(for_command);
            int r=REFRESH_VALUE;
            xQueueSend(key_queue, &r, portMAX_DELAY);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void Task_Key(void *parameter){
    static const int pin[4]={KEY_PIN_UP,KEY_PIN_DOWN,KEY_PIN_VERITY,KEY_PIN_EXIT};
    static const int value[4]={UP,DOWN,VERITY,EXIT};
    int last[4];
    for(int i=0;i<4;i++) last[i]=gpio_get_level(pin[i]);
    while(1){
        for(int i=0;i<4;i++){
            int cur=gpio_get_level(pin[i]);
            if(cur == last[i]){continue;}
            vTaskDelay(pdMS_TO_TICKS(KEY_DEBOUNCE_MS));
            if(gpio_get_level(pin[i])!=cur){continue;}
            last[i]=cur;
            if(cur == PIN_LOW){
                xQueueSend(key_queue, &value[i], portMAX_DELAY);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void Task_OLED_Display(void *parameter){
    uint32_t key_state=UP;
    mList_t *original_list=header.head.next;
    static int oled_pos=OLED_OUTSIDE;
    show_outside(&header,original_list);
    ssd1306_update();
    while(1){
        if(xQueueReceive(key_queue, &key_state, portMAX_DELAY)!=pdTRUE){
            continue;
        }
        if(oled_pos == OLED_OUTSIDE){
            switch(key_state){
                case UP:
                    if(original_list != header.head.next){
                        original_list=original_list->prev;
                    }
                    else{
                        original_list=header.head.prev;
                    }
                    break;
                case DOWN:
                    if(original_list != header.head.prev){
                        original_list=original_list->next;
                    }
                    else{
                        original_list=header.head.next;
                    }
                    break;
                case VERITY:
                    oled_pos=OLED_INSIDE;
                    break;   
                default:
                    show_outside(&header,original_list);
                    ssd1306_update();
                    break;
                
                }
            }
        else if(oled_pos == OLED_INSIDE){
            Node_t *node=container_of(original_list,Node_t,list);
            switch(key_state){
                case UP:
                    xSemaphoreTake(for_command,portMAX_DELAY);
                    node->func(node,1);
                    xSemaphoreGive(for_command);
                    break;
                case DOWN:
                    xSemaphoreTake(for_command,portMAX_DELAY);
                    node->func(node,-1);
                    xSemaphoreGive(for_command);
                    break;
                case EXIT:
                    oled_pos=OLED_OUTSIDE;
                    break;
                default:
                    show_inside(node);
                    ssd1306_update();
                    break;
            }
                
        }
        if(oled_pos == OLED_OUTSIDE){
            show_outside(&header,original_list);
        }
        else{
            Node_t *node=container_of(original_list,Node_t,list);
            show_inside(node);
        }
        ssd1306_update();
        // ESP_LOGI("OLED_Display","unused stack:%u bytes",(unsigned)uxTaskGetStackHighWaterMark(NULL));
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void)
{
    for_command=xSemaphoreCreateMutex();
    if(for_command == NULL){
        printf("[Init] xSemaphoreCreateMutex failed!\n");
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    buf_free[0]=xSemaphoreCreateBinary();//default 0(can not be used)
    buf_free[1]=xSemaphoreCreateBinary();//default 0(can not be used)
    if(buf_free[0] == NULL || buf_free[1] == NULL){
        printf("[Init] xSemaphoreCreateBinary failed!\n");
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    xSemaphoreGive(buf_free[0]);//0->1(can be used)
    xSemaphoreGive(buf_free[1]);//0->1(can be used)
    
    // ---- 1. 外设 初始化 ----
    ssd1306_init(I2C_SDA_PIN, I2C_SCL_PIN);INIT_CHECK_ERR(oled_init_err_cnt,"OLED");
    ssd1306_fill(0);
    ssd1306_update();

    KEY_Init();INIT_CHECK_ERR(key_init_err_cnt,"KEY");
    LED_Init();INIT_CHECK_ERR(led_init_err_cnt,"LED");
    Monitor_Init();INIT_CHECK_ERR(monitor_init_err_cnt,"MONITOR");
    WakeEngine_Init();INIT_CHECK_ERR(wakeup_init_err_cnt,"WAKEUP");
    
    list_init(&header);
    list_insert_tail(&header,&node1);
    list_insert_tail(&header,&node2);
    list_insert_tail(&header,&node3);
    list_insert_tail(&header,&node4);
    if(list_isEmpty(&header)){
        ssd1306_draw_string(0, 16, "List is empty!");
        ssd1306_update();
        return;
    }

    // ---- 2. 先拉起 UI 任务 ----
    //不依赖wifi
    key_queue=xQueueCreate(8, sizeof(int));
    xTaskCreate(Task_OLED_Display, "OLED", 3072, NULL, 1, NULL);
    xTaskCreate(Task_Key, "Key", 2048, NULL, 2, NULL);

    // ---- 3. 音频初始化 ----
    Audio_Init();INIT_CHECK_ERR(audio_init_err_cnt,"AUDIO");
    amp_enable(false);  // 先静音

    // ---- 4. 连接 WiFi ----
    // 注意：WiFi_Connect 内部是 portMAX_DELAY 阻塞等 IP，连不上会一直卡在这句。
    WiFi_SetStateCallBack(wifi_change_to);
    printf("[Init] WiFi connecting...\n");
    WiFi_Connect(WIFI_SSID, WIFI_PASSWORD);
    printf("[Init] WiFi OK\n");

    // ---- 5. 设后端地址 + 分配缓冲区 ----
    voice_set_server(SERVER_IP, SERVER_PORT);
    rec_buf[0] = malloc(MAX_SAMPLES * sizeof(int16_t));
    rec_buf[1] = malloc(MAX_SAMPLES * sizeof(int16_t));
    play_buf = malloc(RECV_MAX   * sizeof(int16_t));
    if (!rec_buf[0] || !rec_buf[1] || !play_buf) {
        printf("[Init] malloc failed! rec[0]=%p rec[1]=%p  play=%p\n", rec_buf[0], rec_buf[1], play_buf);
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    printf("[Init] rec=%d samples  play=%d samples\n", MAX_SAMPLES, RECV_MAX);

    audio_queue=xQueueCreate(1, sizeof(struct msg));
    command_queue=xQueueCreate(1, sizeof(command));
    

    xTaskCreate(Task_AFE_Mic, "Mic", 4096, NULL, 3, NULL);
    xTaskCreate(Task_Handle_Play, "Play", 8192, NULL, 2, NULL);
    xTaskCreate(Task_Command, "Command", 2048, NULL, 1, NULL);

    bool dumped = false;
    while (1){
        //print the stack unsued sizes after ten seconds,just once print
        if (!dumped && xTaskGetTickCount() > pdMS_TO_TICKS(10000)) {
            dump_stack_usage();
            dumped = true;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}


void wifi_change_to(void){
    int r=REFRESH_VALUE;
    xQueueSend(key_queue,&r,0);//不阻塞
}
static void dump_stack_usage(void)
{
    static char buf[2048];   // 放静态区，格式化时不再额外吃任务栈
    vTaskList(buf);
    ESP_LOGI("STACK", "\nTask\t\tState\tPrio\tStack\tNum\tCore\n%s", buf);
}
