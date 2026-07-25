# ESP-IDF 回调函数 —— 以 WiFi 连接为例

## 什么是回调

**你把函数交给系统，系统在合适的时机替你调用。** 你不调它，你只注册它。

```c
// 注册：告诉系统"有事打这个号码找我"
esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                           event_handler,  // ← 你的函数地址
                           NULL);
```

## 为什么不能自己直接调

因为**你根本不知道 WiFi 什么时候启动完、什么时候拿到 IP**。这些是硬件+驱动的异步行为，时间由芯片自己决定，不是你的代码能预判的。

## 对比

```
普通函数调用：                  回调：

你：做番茄炒蛋！                 你：（给机器人一张菜谱）
    ↓                              ↓
你：洗番茄                       你：去睡觉了
    ↓
你：切番茄                       （机器人自己跑菜谱）
    ↓                              ↓
你：打鸡蛋                       WIFI_EVENT_STA_START → 执行菜谱第1步
    ↓                              ↓
你：炒                           IP_EVENT_STA_GOT_IP → 执行菜谱第2步
    ↓                              ↓
你：出锅                         机器人：叮！做好了！
    ↑                              ↑
每一步你亲手做                   你只管交菜谱，时机由机器人掌控
```

## WiFi 回调的完整流程

```
主线程（你的代码）              后台（ESP-IDF 事件循环）
──────────────────              ──────────────────────

wifi_connect()
  ↓
esp_event_handler_register()    登记完成："WIFI_EVENT 来了叫我"
  ↓
esp_wifi_start() ──────────→    WiFi 硬件启动
  ↓                             启动完成
xEventGroupWaitBits()              ↓
  ↓                             遍历回调列表："谁要收 WIFI_EVENT？"
主线程 ██ 睡觉 ██                  ↓
                               找到了！你注册的 event_handler
                                  ↓
                               event_handler(WIFI_EVENT, WIFI_EVENT_STA_START)
                                  ↓
                               esp_wifi_connect() → 开始连接热点
                                  ↓
                               关联成功 → DHCP 拿 IP
                                  ↓
                               拿到 IP！
                                  ↓
                               遍历回调列表："谁要收 IP_EVENT？"
                                  ↓
                               又找到了！event_handler
                                  ↓
                               event_handler(IP_EVENT, IP_EVENT_STA_GOT_IP)
                                  ↓
                               xEventGroupSetBits(wifi_events, WIFI_CONNECTED)
                                  ↓
主线程 ██ 唤醒 ██  ←──────────────┘
  ↓
连接成功，继续执行
```

## 三个事件的触发时机

| 事件 | 谁发的 | 意思 | 你要做什么 |
|------|--------|------|-----------|
| `WIFI_EVENT_STA_START` | WiFi 驱动 | "WiFi 硬件初始化完了，可以连了" | 调用 `esp_wifi_connect()` |
| `WIFI_EVENT_STA_DISCONNECTED` | WiFi 驱动 | "连接断了" | 调用 `esp_wifi_connect()` 重连 |
| `IP_EVENT_STA_GOT_IP` | TCP/IP 栈 | "DHCP 拿到 IP 了，网络通了" | 通知主线程（置位事件组） |

## 事件组配合回调的同步机制

```
回调里:  xEventGroupSetBits(wifi_events, WIFI_CONNECTED)
主线程:  xEventGroupWaitBits(wifi_events, WIFI_CONNECTED, ...)

效果:    回调写完 → 主线程读到 → 线程间同步完成
```
