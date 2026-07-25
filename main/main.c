#include <stdio.h>
#include "wifi.h"
#include "ssd1306.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define I2C_SCL_PIN 20
#define I2C_SDA_PIN 21

// ---------- 改这里 ----------
#define WIFI_SSID      "宇智波皮的iPhone"
#define WIFI_PASSWORD  "77777777"

void app_main(void)
{
    ssd1306_init(I2C_SDA_PIN, I2C_SCL_PIN);
    ssd1306_draw_string(0, 0,  "WiFi connecting...");
    ssd1306_draw_string(0, 16, WIFI_SSID);
    ssd1306_update();

    wifi_connect(WIFI_SSID, WIFI_PASSWORD);

    ssd1306_draw_string(0, 32, "Connected!");
    ssd1306_update();
    printf("WiFi connected!\n");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
