#include "wifi.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
static EventGroupHandle_t wifi_events;
static const int WIFI_CONNECTED = BIT0;
void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data){
    if(base == WIFI_EVENT && id == WIFI_EVENT_STA_START){
        esp_wifi_connect();
    }
    else if(base == IP_EVENT && id == IP_EVENT_STA_GOT_IP){
        xEventGroupSetBits(wifi_events, WIFI_CONNECTED);
    }
    else if(base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED){
        esp_wifi_connect();
    }

}

void WiFi_Init(const char *ssid, const char *password){
    //step1：初始化nvs_flash,wifi驱动所需
    nvs_flash_init();

    //step2：创建事件组
    wifi_events = xEventGroupCreate();

    //step3：初始化网络接口
    esp_netif_init();
    esp_event_loop_create_default_wifi();
    esp_netif_create_default_wifi_sta();

    //step4: 初始化wifi驱动
    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_init_cfg);

    //step5: 注册回调函数
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL);

    //step6: 连接wifi
    wifi_config_t wifi_cfg = {0};
    snprintf((char *)wifi_cfg.sta.ssid, sizeof(wifi_cfg.sta.ssid),
             "%s", ssid);                                   // 填入 WiFi 名
    snprintf((char *)wifi_cfg.sta.password, sizeof(wifi_cfg.sta.password),
             "%s", password);                               // 填入密码
    esp_wifi_set_mode(WIFI_MODE_STA);                       // 设成客户端模式
    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);            // 写入配置
    esp_wifi_start();                                       // 启动 WiFi → 触发 WIFI_EVENT_STA_START
    
    //step7: 阻塞等待，直到拿到 IP
    xEventGroupWaitBits(wifi_events, WIFI_CONNECTED, false, false, portMAX_DELAY);


}