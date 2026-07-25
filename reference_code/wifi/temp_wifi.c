#include "wifi.h"
#include "esp_wifi.h"           // WiFi 驱动核心（连接/扫描/配置）
#include "esp_event.h"          // 事件循环（监听连接状态变化）
#include "nvs_flash.h"          // NVS 闪存（WiFi 配置持久化存储）
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"  // 事件组（线程间同步，等连接完成）

// ---- 事件组：主线程等待 WiFi 连接成功的信号 ----
static EventGroupHandle_t wifi_events;      // 事件组句柄
static const int WIFI_CONNECTED = BIT0;     // bit0 = 连接成功标志

// ================================================================
//  event_handler() — WiFi 事件回调（在 WiFi 任务上下文中执行）
//  ESP-IDF 的事件循环机制：WiFi 驱动在后台自动触发事件，
//  你只需要注册这个回调函数来响应
// ================================================================
static void event_handler(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    // 事件1：WiFi 驱动启动完成 → 立刻发起连接
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();     // 用之前设好的 SSID/密码去连
    }
    // 事件2：连接断开（掉线/路由器重启/信号弱）→ 自动重连
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();     // 重新尝试连接，保证断线自动恢复
    }
    // 事件3：拿到 IP 地址 → 连接真正完成，通知主线程
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_events, WIFI_CONNECTED);  // 置位 bit0，解除阻塞
    }
}

// ================================================================
//  wifi_connect() — 连接 WiFi（阻塞，连上才返回）
//  参数：ssid = WiFi 名称, password = 密码
//  调用后会卡住，直到 WiFi 连上拿到 IP 才继续往下走
// ================================================================
void wifi_connect(const char *ssid, const char *password)
{
    // ----- 1. 初始化 NVS（非易失性存储）-----
    // WiFi 驱动内部需要用 NVS 存校准数据和配置
    nvs_flash_init();

    // ----- 2. 创建事件组（主线程等连接成功的"信号"）-----
    wifi_events = xEventGroupCreate();

    // ----- 3. 初始化 TCP/IP 协议栈 + 创建默认 STA 接口 -----
    esp_netif_init();                       // 初始化 LwIP 网络栈
    esp_event_loop_create_default();        // 创建默认事件循环（回调在这里面跑）
    esp_netif_create_default_wifi_sta();   // 创建 WiFi STA（客户端）接口

    // ----- 4. 初始化 WiFi 驱动 -----
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();  // 默认配置
    esp_wifi_init(&cfg);                                   // 初始化 WiFi 子系统

    // ----- 5. 注册事件回调 -----
    // 告诉 ESP-IDF："WiFi 出任何事，调我的 event_handler"
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                               event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                               event_handler, NULL);

    // ----- 6. 设置 WiFi 并启动 -----
    wifi_config_t wifi_cfg = {0};                           // 配置清零
    snprintf((char *)wifi_cfg.sta.ssid, sizeof(wifi_cfg.sta.ssid),
             "%s", ssid);                                   // 填入 WiFi 名
    snprintf((char *)wifi_cfg.sta.password, sizeof(wifi_cfg.sta.password),
             "%s", password);                               // 填入密码

    esp_wifi_set_mode(WIFI_MODE_STA);                       // 设成客户端模式
    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);             // 写入配置
    esp_wifi_start();                                        // 启动 WiFi → 触发 WIFI_EVENT_STA_START

    // ----- 7. 阻塞等待，直到拿到 IP -----
    // event_handler 收到 GOT_IP 事件后会置位 bit0，这里解除阻塞
    xEventGroupWaitBits(wifi_events, WIFI_CONNECTED,
                        pdFALSE,            // 等完后不清除 bit
                        pdFALSE,            // 不需要所有 bit 都满足
                        portMAX_DELAY);     // 无限等待
                        
}
