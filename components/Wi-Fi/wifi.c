#include "wifi.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_check.h"          // ESP_RETURN_VOID_ON_ERROR / ESP_RETURN_VOID_ON_FALSE
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

// just for this file
static const char *TAG_WIFI = "WIFI";

static EventGroupHandle_t wifi_events;
static const int WIFI_CONNECTED = BIT0;
char *wifi_str_state[2] = {"NOT_OK","OK"};
void (*wifi_state_cb)(void) = NULL;

// 事件处理函数
void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data){
    if(base == WIFI_EVENT && id == WIFI_EVENT_STA_START){
        esp_wifi_connect();
    }
    else if(base == IP_EVENT && id == IP_EVENT_STA_GOT_IP){
        xEventGroupSetBits(wifi_events, WIFI_CONNECTED);
        if(wifi_state_cb){
            wifi_state_cb();
        }
    }
    else if(base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED){
        xEventGroupClearBits(wifi_events, WIFI_CONNECTED);
        if(wifi_state_cb){
            wifi_state_cb();
        }
        esp_wifi_connect();
    }
}

void WiFi_Connect(const char *ssid, const char *password){
    esp_err_t ret;

    //step1：初始化nvs_flash,wifi驱动所需
    // 刷过新固件后 NVS 分区版本可能对不上，这时要擦掉重建，否则以后升级固件会卡在这
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG_WIFI, "nvs_flash_init: %s, erasing NVS and retrying", esp_err_to_name(ret));
        ESP_RETURN_VOID_ON_ERROR(nvs_flash_erase(), TAG_WIFI, "nvs_flash_erase failed");
        ret = nvs_flash_init();
    }
    ESP_RETURN_VOID_ON_ERROR(ret, TAG_WIFI, "nvs_flash_init failed: %s", esp_err_to_name(ret));

    //step2：创建事件组
    // xEventGroupCreate 失败返回 NULL
    wifi_events = xEventGroupCreate();
    ESP_RETURN_VOID_ON_FALSE(wifi_events != NULL, TAG_WIFI, "xEventGroupCreate failed");

    //step3：初始化网络接口
    ret = esp_netif_init();
    ESP_RETURN_VOID_ON_ERROR(ret, TAG_WIFI, "esp_netif_init failed: %s", esp_err_to_name(ret));
    ret = esp_event_loop_create_default();
    ESP_RETURN_VOID_ON_ERROR(ret, TAG_WIFI, "esp_event_loop_create_default failed: %s", esp_err_to_name(ret));
    // 注意：这个函数返回的是 esp_netif_t* 指针，不是 esp_err_t，失败时返回 NULL
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    ESP_RETURN_VOID_ON_FALSE(sta_netif != NULL, TAG_WIFI, "esp_netif_create_default_wifi_sta failed");

    //step4: 初始化wifi驱动0
    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&wifi_init_cfg);
    ESP_RETURN_VOID_ON_ERROR(ret, TAG_WIFI, "esp_wifi_init failed: %s", esp_err_to_name(ret));

    //step5: 注册回调函数
    ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL);
    ESP_RETURN_VOID_ON_ERROR(ret, TAG_WIFI, "esp_event_handler_register(WIFI_EVENT) failed: %s", esp_err_to_name(ret));
    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL);
    ESP_RETURN_VOID_ON_ERROR(ret, TAG_WIFI, "esp_event_handler_register(IP_EVENT) failed: %s", esp_err_to_name(ret));

    //step6: 连接wifi
    wifi_config_t wifi_cfg = {0};
    snprintf((char *)wifi_cfg.sta.ssid, sizeof(wifi_cfg.sta.ssid),
             "%s", ssid);                                   // 填入 WiFi 名
    snprintf((char *)wifi_cfg.sta.password, sizeof(wifi_cfg.sta.password),
             "%s", password);                               // 填入密码
    ret = esp_wifi_set_mode(WIFI_MODE_STA);                        // 设置客户端模式
    ESP_RETURN_VOID_ON_ERROR(ret, TAG_WIFI, "esp_wifi_set_mode failed: %s", esp_err_to_name(ret));
    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);            // 写入配置
    ESP_RETURN_VOID_ON_ERROR(ret, TAG_WIFI, "esp_wifi_set_config failed: %s", esp_err_to_name(ret));
    ret = esp_wifi_start();                                       // 启动 WiFi → 触发 WIFI_EVENT_STA_START
    ESP_RETURN_VOID_ON_ERROR(ret, TAG_WIFI, "esp_wifi_start failed: %s", esp_err_to_name(ret));

    //step7: 阻塞等待，直到拿到 IP
    xEventGroupWaitBits(wifi_events, WIFI_CONNECTED, false, false, portMAX_DELAY);


}

bool WiFi_isConnect(void){
    if(wifi_events == NULL){
        return false;
    }
    return (xEventGroupGetBits(wifi_events) & WIFI_CONNECTED) != 0;
}