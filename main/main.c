#include <stdio.h>
#include <stdlib.h>
#include "audio.h"
#include "wifi.h"
#include "ssd1306.h"
#include "voice_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ---- 改这里 ----
#define WIFI_SSID      "宇智波皮的iPhone"
#define WIFI_PASSWORD  "77777777"
#define SERVER_IP      "172.20.10.3"   // 手机热点：172.20.10.x，连同一热点后查电脑IP
#define SERVER_PORT    8006
#define RECORD_SEC     4                // 录音秒数

#define I2C_SCL_PIN    20
#define I2C_SDA_PIN    21
#define MAX_SAMPLES    (SAMPLE_RATE * RECORD_SEC)      // 录音缓冲大小
#define RECV_MAX       (SAMPLE_RATE * 20)              // 接收缓冲最大 20 秒

static int16_t *rec_buf  = NULL;   // 录音缓冲（malloc 到 PSRAM）
static int16_t *play_buf = NULL;   // 播放缓冲

void app_main(void)
{
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

    // ---- 5. 主循环：录音 → 发送 → 收回复 → 播放 ----
    char line1[22] = {0};
    char line2[22] = {0};

    while (1) {
        // ====== 5a. 等待按键触发（简化为等 1 秒）======
        ssd1306_clear_row(0);
        ssd1306_clear_row(16);
        snprintf(line1, sizeof(line1), "Ready.  %ds rec", RECORD_SEC);
        ssd1306_draw_string(0, 0,  line1);
        ssd1306_draw_string(0, 16, "Press key...");
        ssd1306_update();
        vTaskDelay(pdMS_TO_TICKS(1000));

        // ====== 5b. 录音 ======
        amp_enable(false);
        ssd1306_clear_row(24);
        ssd1306_draw_string(0, 24, "Recording...");
        ssd1306_update();

        for (int i = 0; i < MAX_SAMPLES; i++) {
            rec_buf[i] = mic_read();
        }
        printf("[录音] %d samples done\n", MAX_SAMPLES);

        // ====== 5c. 发送到后端 ======
        ssd1306_clear_row(24);
        ssd1306_draw_string(0, 24, "Sending to AI...");
        ssd1306_update();

        size_t reply_len = RECV_MAX;
        bool ok = voice_send_receive(rec_buf, MAX_SAMPLES, play_buf, &reply_len);

        if (!ok || reply_len == 0) {
            ssd1306_clear_row(24);
            ssd1306_draw_string(0, 24, "No reply       ");
            ssd1306_update();
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        // ====== 5d. 显示识别结果 ======
        const char *user = voice_last_user_text();
        const char *ai   = voice_last_ai_text();
        printf("[结果] 用户: %s\n", user);
        printf("[结果] AI:   %s\n", ai);

        snprintf(line1, sizeof(line1), "%.20s", user);
        snprintf(line2, sizeof(line2), "%.20s", ai);
        ssd1306_clear_row(0);
        ssd1306_clear_row(16);
        ssd1306_draw_string(0, 0,  line1);
        ssd1306_draw_string(0, 16, line2);

        // ====== 5e. 播放 AI 回复 ======
        ssd1306_clear_row(32);
        ssd1306_draw_string(0, 32, "Playing reply...");
        ssd1306_update();

        amp_enable(true);
        spk_write(play_buf, reply_len);
        amp_enable(false);

        ssd1306_clear_row(32);
        ssd1306_draw_string(0, 32, "Done.");
        ssd1306_update();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
