# ESP32-S3 软件调试笔记（语音助手 + 语音控制外设）

> 硬件/音频调试见 [debug-notes.md](debug-notes.md)。
> 本文档记录本项目软件开发过程中遇到的所有问题，每条包含：**现象 / 原因 / 解决方法**。

## 环境

- 芯片：ESP32-S3 N16R8（16MB Flash，8MB Octal PSRAM）
- IDF 版本：v5.4.4
- 架构：FreeRTOS 多任务（录音 / 按键 / HTTP 收发 / OLED / 指令）
- 后端：Python FastAPI（ASR → LLM → TTS，返回 PCM + X-Command 头部）
- 语音控制链路：服务器检测关键词 → `X-Command` 头 → ESP32 解析 → 驱动 GPIO

---

# 一、系统 / 启动类

## 问题 1：开机崩溃，无法进入 `app_main`

### 现象

烧录后串口刷屏，打印类似 `SPIRAM` 相关错误或直接 panic 重启。

### 原因

N16R8 模组的 PSRAM 是 **Octal**（8 线）模式，但 sdkconfig 里默认配置成了 QUAD。硬件不匹配导致 PSRAM 初始化失败，启动阶段崩溃。

### 解决方法

在 menuconfig 中把 PSRAM 模式改成 Octal：

```
idf.py menuconfig
  → Component config
    → ESP PSRAM
      → SPI RAM config
        → Mode (QUAD → OCT)
```

对应 sdkconfig：`CONFIG_SPIRAM_MODE_OCT=y`

---

## 问题 2：烧录时报端口占用 / 烧完没反应

### 现象

- 点击烧录提示端口被占用（COM3 busy）
- 或烧录成功但串口无输出，芯片"死了"

### 原因

串口监视器占用了 COM 口，或者芯片停留在 bootloader 下载模式没进入运行。

### 解决方法

1. 先关掉 `idf.py monitor`（串口监视器），再烧录
2. 重新插拔 USB 线
3. 烧录时按住开发板 **BOOT** 键再松手，强制进入下载模式

---

## 问题 3：Flash 容量告警

### 现象

编译烧录时出现：

```
Detected size(16384k) larger than image header(2048k)
```

### 原因

芯片实际是 16MB Flash，但工程配置成 2MB，镜像头与硬件不符。

### 解决方法

```
idf.py menuconfig
  → Serial flasher config
    → Flash size → 16MB
```

---

# 二、FreeRTOS 任务类

## 问题 4：任务名太长，断言崩溃

### 现象

创建任务时崩溃，断言报错：

```
assert failed: xTaskCreate / strlen(pxName) < configMAX_TASK_NAME_LEN
```

### 原因

FreeRTOS 任务名最长 15 个字符（含结束符 < 16）。`"Task_OLED_Display"` 有 17 个字符，超长触发断言。

### 解决方法

任务名改短，同时用句柄保存（`&oled_task` 用于后续通知）：

```c
xTaskCreate(Task_Record,       "Rec",   2048, NULL, 4, NULL);
xTaskCreate(Task_Key,          "Key",   2048, NULL, 3, NULL);
xTaskCreate(Task_Handle_Play,  "Play",  8192, NULL, 2, NULL);
xTaskCreate(Task_OLED_Display, "OLED",  2048, NULL, 1, &oled_task);
xTaskCreate(Task_Command,      "Cmd",   2048, NULL, 1, NULL);
```

---

## 问题 5：使用通知索引 1 时断言崩溃

### 现象

一按录音按钮就卡死/重启，串口报：

```
assert failed: xTaskGenericNotifyWait (uxIndexToWait < 1)
```

指向 `main.c` 里 `xTaskNotifyWaitIndexed(1, ...)` 那一行。

### 原因

FreeRTOS 的**任务通知数组默认只有 1 个槽位（索引 0）**。代码用了索引 0 和索引 1 两个通知（一个显示状态、一个显示指令），越界触发断言。

### 解决方法

把通知数组条目数改为 2：

```
idf.py menuconfig
  → Component config
    → FreeRTOS
      → Kernel
        → configTASK_NOTIFICATION_ARRAY_ENTRIES → 2
```

---

## 问题 6：OLED 不显示状态（双阻塞）

### 现象

`Task_OLED_Display` 里依次 `xTaskNotifyWaitIndexed(0,...)` 和 `xTaskNotifyWaitIndexed(1,...)`，OLED 一直不更新状态。

### 原因

两个等待都用了 `portMAX_DELAY`（永久阻塞）。第一个通知到了会被消费掉，但随后永久阻塞在第二个等待上；更糟的是两个阻塞都永久等待，显示永远不刷新。

### 解决方法

改成有限超时，两个通知轮询式获取：

```c
xTaskNotifyWaitIndexed(0, 0, 0, &state,  pdMS_TO_TICKS(100));
xTaskNotifyWaitIndexed(1, 0, 0, &command, pdMS_TO_TICKS(100));
```

---

## 问题 7：指令队列长度错误

### 现象

语音指令（`led_on` / `led_off`）发出后，`Task_Command` 收到的数据错乱或收不到。

### 原因

队列创建时用了 `sizeof(char *)`（4 字节），但实际发送的是一个 `char[16]` 数组，队列只能装下 4 字节，数据被截断/损坏。

### 解决方法

队列大小必须用实际发送的数据大小：

```c
char command[16];
command_queue = xQueueCreate(1, sizeof(command));   // 16 字节
```

---

## 问题 8：字符串越界写

### 现象

复制指令字符串后，偶发乱码或内存损坏。

### 原因

```c
command[sizeof(command)] = '\0';   // 越界！下标 0~15 才是合法范围
```

### 解决方法

```c
strncpy(command, voice_last_command(), sizeof(command) - 1);
command[sizeof(command) - 1] = '\0';   // 最后一个下标，不是 sizeof
```

---

## 问题 9：按键松开检测不到（`KEY_RELEASED` 永不触发）

### 现象

按住录音正常，但松手后录音不停止，一直录到超时。

### 原因

按键扫描间隔（20ms）错过了松开瞬间的电平变化：上一次扫描还是按下，这一次已经是松开后的"未按下"状态，但中间态（KEY_RELEASED 枚举）没被任何一次扫描捕获。

### 解决方法

不做状态枚举判断，改为**边沿检测**（前后两次电平对比）：

```c
status = KEY_De_trembing(KEY_PIN);
if (status == KEY_PRESSED && !last_status) {      // 按下沿
    cmd = CMD_START_REC;
    xQueueSend(rec_queue, &cmd, portMAX_DELAY);
} else if (status == KEY_NOT_PRESSED && last_status) {  // 松开沿
    cmd = CMD_STOP_REC;
    xQueueSend(rec_queue, &cmd, portMAX_DELAY);
}
last_status = (status == KEY_PRESSED);
```

---

# 三、HTTP / 语音服务器类

## 问题 10：`esp_http_client_get_response_header()` 未声明

### 现象

编译报错：函数隐式声明（implicit declaration）。

### 原因

想用该 API 读响应头，但 **IDF v5.4.4 已经移除了这个接口**，头文件里没有声明。

### 解决方法

改用事件回调 `HTTP_EVENT_ON_HEADER` 主动捕获头部字段（见问题 12）。

---

## 问题 11：`esp_http_client_perform()` 返回成功但读到 0 字节

### 现象

`perform()` 返回后直接读 buffer，读不到任何数据。

### 原因

`perform()` 内部已经**消费掉了整个响应体**。它在事件回调里把数据递给你，之后自己不再保存，等 perform 返回再读自然为空。

### 解决方法

在 `HTTP_EVENT_ON_DATA` 事件回调里把数据拷进自己的接收缓冲：

```c
case HTTP_EVENT_ON_DATA:
    if (g_recv_buf && (g_recv_total + evt->data_len) <= g_recv_max) {
        memcpy(g_recv_buf + g_recv_total, evt->data, evt->data_len);
        g_recv_total += evt->data_len;
    }
    break;
```

注意回调是全局的，需要在发送前设置好接收指针/容量，结束后清空避免野指针。

---

## 问题 12：`X-Command` 头部捕获不到，LED 不动作

### 现象

服务器明明发了 `X-Command: led_on` 头，串口却不打印 `[HTTP] 指令:`，LED 没反应。

### 原因（两个坑叠加）

1. 回调里拿错了字段：
   ```c
   strcmp(evt->header_value, "X-Command")   // 错！value 是值不是键
   ```
   应该比较 `evt->header_key`，而不是 `header_value`。

2. **esp_http_client 会把头部键名全部转成小写**。`X-Command` 在回调里实际是 `x-command`，用大写字符串比较永远不相等。

### 解决方法

```c
else if (strcmp(evt->header_key, "x-command") == 0) {
    strncpy(g_command, evt->header_value, sizeof(g_command) - 1);
}
```

规律：**回调里所有键都用小写比较**（`x-user-text`、`x-ai-text`、`x-command`）。

---

## 问题 13：`Software caused connection abort`（HTTP 请求失败）

### 现象

串口打印请求失败，错误码 `ESP_ERR_HTTP_CONNECT` 或连接中断。

### 原因

后端服务器没启动，或 IP 不在同一网络（手机热点网段对不上）。

### 解决方法

1. 确认 `python_server/server.py` 已启动
2. 确认 `SERVER_IP` 是电脑在当前热点的 IP（`172.20.10.x`）
3. 电脑和 ESP32 连同一个热点

---

## 问题 14：服务器返回 204 No Content（识别为空）

### 现象

发送录音后服务器响应 204，没有返回任何内容。

### 原因

ASR 识别出的文字为空，通常是录音阶段的问题：麦克风没采到有效语音（离嘴太远 / 声音太小 / 没说话就松手）。

### 解决方法

- 检查麦克风接线与数据（参考 debug-notes.md 音频问题）
- 说话清楚、靠近麦克风再按键
- 先看串口 `[HTTP] 用户:` 是否打印了识别文字

---

## 问题 15：服务器端 `AttributeError: 'bytes' object has no attribute 'encode'`

### 现象

FastAPI 返回响应时 Python 报错，ESP32 收不到回复。

### 原因

把**中文**直接放进 HTTP 头部值。HTTP 头只允许 ASCII/latin-1，中文会触发编码错误（latin-1 无法表示中文）。

### 解决方法

对中文做 **URL 编码**后再放入头部：

```python
from urllib.parse import quote
headers = {
    "X-User-Text": quote(user, safe=""),
    "X-AI-Text":   quote(ai,   safe=""),
}
```

---

## 问题 16：DeepSeek 返回 400（模型错误）

### 现象

服务器调用 DeepSeek API 报 400，AI 不回复。

### 原因

请求里模型名写错（IDF 环境无关，是后端 API 配置问题）。

### 解决方法

改成正确的模型名（本项目用 `deepseek-v4-pro`）。

---

# 四、音频播放类

## 问题 17：播放声音模糊（I2S 使能顺序错误）

### 现象

播放/直通时声音模糊或异常。

### 原因

RX 先于 TX 使能。I2S 的 **TX 是时钟主**，RX 从属，必须先开 TX 再开 RX。

### 解决方法

```
i2s_channel_enable(tx_handle);
i2s_channel_enable(rx_handle);
```

---

## 问题 18：AI 回复被截断 + 急促杂音（三个 bug 叠加）

### 现象

AI 回复只播了一小段就没了，或者播放出急促刺耳杂音。

### 原因（三个 bug 叠加）

1. **接收容量太小**：把录音长度当成接收缓冲容量传进去。AI 回复通常比录音大，超出容量的部分被事件回调丢弃 → 截断。
2. **回填长度错变量**：
   ```c
   voice_send_receive(rec_buf, rec_len, play_buf, &rec_len);  // 错！
   ```
   传了 `&rec_len` 而不是 `&play_len`，导致 `play_len` 保持初始值 `RECV_MAX`（32 万采样），`spk_write()` 把整块未初始化的内存当音频播 → 20 秒杂音。
3. **功放关早了**：`amp_enable(false)` 紧跟 `spk_write()`。`i2s_channel_write` 返回时数据只进了 DMA，还没从引脚播完，功放提前断电 → 尾部"咔"一下被切断。

### 解决方法

```c
play_len = RECV_MAX;                                    // 1. 容量给足
bool ok = voice_send_receive(rec_buf, rec_len,
                             play_buf, &play_len);       // 2. 回填 play_len
...
spk_write(play_buf, play_len);
vTaskDelay(pdMS_TO_TICKS(300));                          // 3. 等 DMA 播完
amp_enable(false);
```

---

# 五、外设控制类

## 问题 19：LED 电平与直觉相反

### 现象

写 `gpio_set_level(PIN, 1)` 想让 LED 亮，结果灭了；写 0 才亮。

### 原因

LED 接法是**低电平点亮**（active-low）：GPIO 输出 0 → 灯亮，输出 1 → 灯灭。`LED_Init` 里初始化电平为 1（默认灭）。

### 解决方法

```c
Set_Level_LED(PIN, 0);   // 亮
Set_Level_LED(PIN, 1);   // 灭
```

---

## 问题 20：电机不转（TB6612FNG 电源问题）

### 现象

`motor_set_speed()` 设置了占空比，但电机完全不转。用万用表量 TB6612 的 VM 脚只有 ~2V。

### 原因

开发板丝印 `5Vm` 的引脚**不是能带载的 5V 输出**，实测空载才 2V 左右，带不动电机。这是硬件供电问题，不是软件问题。

### 解决方法

- 用**独立的锂电池**给电机驱动供电（TB6612FNG VM 支持 2.5~13.5V）
- 电机电源地和 ESP32 地必须**共地**
- STBY 脚接 3.3V（高电平使能）
- AIN1/AIN2 决定方向，PWMA 决定速度（占空比）

> 电机模块已写好在 `components/Monitor/`，等锂电池到位后接入即可。

---

# 调试经验总结

## 通用排查顺序

1. **先看串口日志**：ESP32 端用 `printf` 打印关键节点（请求状态、收到的指令、长度）
2. **先软件后硬件**：用万用表测电压前，先确认代码逻辑（参数传递、长度回填）
3. **怀疑库行为前先查文档**：如 esp_http_client 会小写头部键、perform 会消费响应体、通知数组默认只有 1 个槽位

## 本项目容易踩的坑（速查）

| 坑 | 正确写法 |
|----|----------|
| 头部键比较 | 一律小写 `x-command` |
| 响应体获取 | 在 `HTTP_EVENT_ON_DATA` 里拷，别等 perform 返回 |
| 通知索引 | `configTASK_NOTIFICATION_ARRAY_ENTRIES = 2` |
| 队列大小 | `sizeof(实际数据类型)` |
| 字符串结尾 | `buf[sizeof(buf)-1]='\0'` |
| 中文进 HTTP 头 | `quote(..., safe="")` 编码 |
| 功放关闭 | `spk_write` 后延时 300ms |
| I2S 使能 | 先 TX 后 RX |
