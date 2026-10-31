# 9 小时稳定性测试记录

测试日期：2026-10-31 01:52 ~ 11:00
固件：ESP-IDF v5.4.4，目标 esp32s3
日志：`run.log`（`idf.py -p COM3 monitor > run.log 2>&1`）

## 一、测试目的

验证系统在长时间运行下是否存在：

1. 内存泄漏（internal heap / PSRAM 是否持续下降）
2. 温度失控（芯片是否发烫）
3. 网络/串口断连
4. 多轮对话后性能退化

## 二、测试环境

| 项目 | 配置 |
|---|---|
| 开发板 | ESP32-S3，16 MB flash，八线 PSRAM（40 MHz） |
| 供电 | Type-C 接 PC，全程不断电 |
| 网络 | 电脑热点，服务端跑在同一台 PC 上 |
| 服务端 | FastAPI + uvicorn :8006，ASR = paraformer-realtime-v2，LLM = DeepSeek，TTS = edge-tts + ffmpeg |
| 采样 | `app_main` 主循环每 10 分钟打印一次 heap / PSRAM / 温度，并 `vTaskList` 打印任务栈 |
| 音频缓冲 | rec_buf[2] / play_buf 全部在 PSRAM 预分配 |

### 采样口径

- 内部堆：`heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)`
  —— 不能用 `esp_get_free_heap_size()`，那个会把 PSRAM 也算进去（会显示 5.7 MB）
- PSRAM：`heap_caps_get_free_size(MALLOC_CAP_SPIRAM)`
- `min` 是**自本次上电以来**的最低水位，不是当前值
- 温度：`temperature_sensor_get_celsius()`（量程 20–100 °C）
- 栈剩余：`uxTaskGetStackHighWaterMark()` 在 ESP-IDF 里返回**字节数**（不是字）

## 三、测试流程

1. 01:52:12 上电启动，作为计时零点
2. 01:52 ~ 10:47 纯待机，不交互（**8 小时 55 分**）
3. 10:47 ~ 10:56 连续对话 14 次（含长回复、短指令、诗词、闲聊）
4. 11:00 结束，全程未重启、未拔插 USB

## 四、结果

### 4.1 内存（每小时采样）

| 运行时长 | heap cur | heap min | PSRAM cur | 温度 |
|---|---|---|---|---|
| 0h10m | 180,239 | 169,815 | 5,590,216 | 34.2 °C |
| 1h00m | 179,919 | 167,639 | 5,590,216 | 35.2 °C |
| 2h00m | 179,919 | 167,639 | 5,590,216 | 36.2 °C |
| 3h00m | 179,963 | 167,627 | 5,590,216 | 33.2 °C |
| 4h00m | 179,963 | 167,627 | 5,590,216 | 33.2 °C |
| 5h00m | 179,963 | 167,627 | 5,590,216 | 33.2 °C |
| 6h00m | 179,963 | 167,627 | 5,590,216 | 33.2 °C |
| 7h00m | 179,963 | 167,627 | 5,590,216 | 32.2 °C |
| 8h00m | 179,963 | 167,511 | 5,590,216 | 32.2 °C |
| 9h00m | 178,807 | 156,923 | 5,590,216 | 35.2 °C |
| 9h10m | 179,691 | 156,923 | 5,590,216 | 33.2 °C |

（共 55 条采样，间隔 600 s，无缺失。9h00m 那一行处在对话刚结束、连接尚未释放的时刻；9h10m 是空闲 10 分钟后的读数。）

**结论：**

- **PSRAM 完全没动**：cur 始终 5,590,216，对话期间 min 只下探 172 B（5,590,044）—— 音频缓冲全部预分配，符合预期
- **内部堆无泄漏**：待机 9 小时内 cur 稳定在 179,963，抖动 ±44 B（属于 lwIP / 日志缓冲的正常波动），没有任何单调下降
- **对话峰值**：156,923 B，比待机最低水位（167,511）再低 10.6 KB —— 这是 HTTP 收发缓冲的瞬时占用，对话结束即释放
- **对话后的残留**：对话刚结束采样到 cur = 178,807（比整夜平台低 1,156 B），空闲 10 分钟后再采样为 179,691（低 272 B）。说明那 1,156 B 主要是连接尚未释放的瞬时占用，不是泄漏；272 B 属于碎片或一次性分配的量级，可以忽略

### 4.2 温度

整夜范围 **32.2 ~ 37.2 °C**，开机和对话结束后略高，随后回落，9 小时没有缓慢上升趋势。

体感"外壳烫手"来自喇叭和线性稳压器，与 SoC 无关，芯片离热保护仍有很大余量。

### 4.3 任务栈（对话后剩余）

| 任务 | 剩余字节 |
|---|---|
| play | 5,508 |
| mic | 1,784 |
| Command | 2,080 |
| OLED | 1,640 |
| Key | 1,220 |

9 小时无变化。

### 4.4 对话测试（14 次唤醒）

| # | 识别文本 | rec | net | total | 结果 |
|---|---|---|---|---|---|
| 1 | （无） | 0.48 | — | — | 400 |
| 2 | 已是悬崖百丈冰的下一。 | 1.89 | 7.26 | 9.15 | 200 |
| 3 | 雄关漫道真如铁。接下一句。 | 3.72 | 6.64 | 10.36 | 200 |
| 4 | 怎么看待open night爱解决数学难题这件事？ | 4.98 | 10.29 | 15.27 | 200 |
| 5 | 你的创作者是谁？ | 2.46 | 6.40 | 8.86 | 200 |
| 6 | 哪家大模型最强？ | 3.15 | 7.80 | 10.95 | 200 |
| 7 | 解释一下一万年太久只争朝夕的含义。 | 4.65 | 7.58 | 12.23 | 200 |
| 8 | （无） | 0.48 | — | — | 400 |
| 9 | （无） | 0.75 | — | — | 400 |
| 10 | 你觉得未来就业率会变低吗？ | 3.54 | 8.33 | 11.87 | 200 |
| 11 | 开灯。 | 1.62 | 5.83 | 7.45 | 200 |
| 12 | 关灯。 | 1.59 | 3.94 | 5.53 | 200 |
| 13 | 蒸馏是什么意思？ | 2.55 | 7.15 | 9.70 | 200 |
| 14 | 县城和进城的区别。 | 3.00 | 8.30 | 11.30 | 200 |

单位：秒。`rec` = 唤醒到录音结束，`net` = 录音结束到收到 PCM，`total` = rec + net。
注：`net` 里可能包含等待上一条回复播放完成的时间。

**统计：**

- 成功率 11/14，3 次失败全部是 `HTTP 400`
- net 范围 **3.94 ~ 10.29 s**，中位数约 7.5 s
- total 范围 5.53 ~ 15.27 s

对比前一次（同日 01:00 左右）的 12 轮测试：net 为 8.75 ~ 14.55 s。同样的设备和固件，白天明显更快，说明夜间延迟主要来自网络与 LLM，而不是设备侧。

### 4.5 3 次 400 的原因

服务端 `server.py` 有长度校验：

```python
if len(pcm_body) < sample_rate * bit_depth:   # 16000 * 2 = 32000 字节
    return Response(content=b"", status_code=400)
```

即音频不足 1 秒直接拒绝。这 3 次对应 0.48 s / 0.48 s / 0.75 s，是唤醒后只说了唤醒词或发音过短，VAD 立即判定静音结束。

设备端目前只打印 `E play: voice_send_receive failed!`，用户侧没有任何反馈。

## 五、稳定性事件

| 项目 | 次数 |
|---|---|
| 重启 | 0（只有 01:52 的一条 `rst:0x1 (POWERON)`） |
| WiFi 断连 / 重连 | 0 |
| 串口断连 | 0 |
| 看门狗复位 | 0 |
| 除 400 外的告警/错误 | 0（另有一条开机的 WiFi authmode 提示，属正常） |

上一次测试中出现过的串口错误（`GetOverlappedResult failed (PermissionError 13)`，原因是 VS Code 的 ESP-IDF Monitor 占用了 COM3）本次未复现，测试期间未打开 VS Code。

## 六、结论

1. **无内存泄漏**：PSRAM 恒定，内部堆 9 小时无下降趋势
2. **无热问题**：SoC 32 ~ 37 °C，可以长时间供电运行
3. **链路稳定**：9 小时零断连、零复位
4. **性能瓶颈在网络侧**：设备侧 rec 稳定在 1.6 ~ 5.0 s，其余 4 ~ 10 s 全部消耗在服务端 ASR/LLM/TTS 与传输上

## 七、待改进项

1. **短语音过滤**：设备端发包前判断 `samples >= 16000`（不足 1 秒直接丢弃），省掉一次无意义的往返；丢弃时必须归还 `buf_free[rec_buf_index]` 信号量，且不切换缓冲索引
2. **失败反馈**：400 / 204 / 超时三种情况目前都是静默失败，建议 OLED 提示或播放固定提示音
3. ~~1,156 B 差值定性~~：已由空闲后的采样排除，无需再查
4. **延迟优化**：LLM 占大头，可考虑缩短 `max_tokens`、流式输出或本地小模型

## 八、复现步骤

```powershell
# 1. 防止电脑睡眠（否则 USB 串口断开、板子可能掉电）
powercfg /change standby-timeout-ac 0
powercfg /change hibernate-timeout-ac 0
powershell
# 2. 导入 ESP-IDF 环境（必须有，否则 idf.py 会走 .py 的文件关联被 VS Code 接管）
& 'c:\Users\32092\.vscode\extensions\espressif.esp-idf-extension-2.3.0\export.ps1'

# 3. 开始记录（前台运行，会一直占住窗口）
cd D:\ESP32_project\Chat_Assistant
idf.py -p COM3 monitor > run.log 2>&1
```

停止：`Ctrl + C`。

注意：

- PowerShell 的 `>` 重定向输出是 UTF-16LE，读取时需要转换：`iconv -f UTF-16LE -t UTF-8 run.log`
- 测试期间不要打开 VS Code，其 ESP-IDF 插件会抢占 COM3
- 不要合盖（合盖睡眠与电源超时设置无关）

## 附录 A：测量用到的 API

### A.1 内存

| API | 作用 | 单位 |
|---|---|---|
| `heap_caps_get_free_size(MALLOC_CAP_INTERNAL \| MALLOC_CAP_8BIT)` | 当前内部 RAM 堆剩余 | 字节 |
| `heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL \| MALLOC_CAP_8BIT)` | 自本次上电以来内部堆的**最低水位** | 字节 |
| `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` | 当前 PSRAM 剩余 | 字节 |
| `heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM)` | 自本次上电以来 PSRAM 最低水位 | 字节 |
| `heap_caps_malloc(size, MALLOC_CAP_SPIRAM \| MALLOC_CAP_8BIT)` | 把录音/播放缓冲分配到 PSRAM | 返回指针 |

代码位置：`main.c:405-413`（启动时打印一次）、`main.c:438-443`（每 10 分钟打印一次）。

**坑：**

- 不能用 `esp_get_free_heap_size()` / `esp_get_minimum_free_heap_size()`。它们默认带 `MALLOC_CAP_DEFAULT` 能力集，**会把 PSRAM 一起算进"堆"**，在 8 MB PSRAM 的板子上会显示 5.7 MB，看不出内部 RAM 的真实余量
- `minimum_free` 是"自本次上电"的全局最低值，**重启即清零**，不能跨重启比较
- 这两组 API 的数值差异只有几 KB 时属于碎片和临时连接状态，不要急着判定泄漏；判泄漏要看多次采样的**趋势**（本例 9 小时 `cur` 恒定在 179,963 ± 44 B）

### A.2 任务栈

| API | 作用 | 单位 |
|---|---|---|
| `uxTaskGetStackHighWaterMark(NULL)` | 当前任务自创建以来的**最小剩余栈空间** | **字节** |
| `uxTaskGetStackHighWaterMark(handle)` | 指定任务的最小剩余栈空间 | 字节 |
| `vTaskList(char *buf)` | 打印所有任务的状态/优先级/剩余栈/编号/运行核 | 文本 |

代码位置：`main.c:210-211`（每轮对话后打印 play/mic）、`main.c:455-460`（`dump_stack_usage()`，`vTaskList` 写入静态缓冲区）。

**坑：**

- **IDF 里单位是字节**，因为 `StackType_t` 是 `uint8_t`；上游 FreeRTOS 是 `StackType_t` 为 4 字节的 MCU，返回的是"字"。照搬网上例子会把栈用量估小 4 倍
- `vTaskList` 需要 `CONFIG_FREERTOS_USE_TRACE_FACILITY`，输出带核号还需要 `CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID`
- 输出缓冲区要放在静态区（本例 `static char buf[2048]`），否则格式化的十几行文本会吃掉任务栈本身

### A.3 温度

```c
temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
temperature_sensor_install(&cfg, &tsens);   // 安装
temperature_sensor_enable(tsens);           // 使能
temperature_sensor_get_celsius(tsens, &chip_temp);  // 读取，单位 float 摄氏度
```

代码位置：`main.c:420-425`（初始化）、`main.c:437`（每 10 分钟读取）。

**坑：**

- 必须先 `install` 再 `enable`，否则 `get_celsius` 返回未初始化错误
- 量程在 `TEMPERATURE_SENSOR_CONFIG_DEFAULT(min, max)` 里确定（本例 20–100 °C），超出量程的读数不可信
- 这个传感器测的是 **SoC 片内温度**，不是外壳或喇叭温度，体感"烫手"不能用来判断芯片是否过热

### A.4 计时与延迟打点

| API/变量 | 作用 |
|---|---|
| `esp_timer_get_time()` | 返回自启动以来的**微秒数**（`int64_t`），本次所有延迟都以它为时基 |
| `ESP_LOGI` 前缀 `I (xxxxx)` | 系统启动后的**毫秒数**，用于把日志和打点对齐 |

四个打点（`struct msg` 中的 `t0/t1` + 全局 `t2/t3`）：

| 打点 | 位置 | 含义 |
|---|---|---|
| `t0` | 唤醒词命中、获取录音缓冲**之前** | 用户说完唤醒词的时刻 |
| `t1` | 录音结束、消息入队之前 | 一句话说完（VAD 判定静音）的时刻 |
| `t2` | `voice_send_receive()` 返回之后 | 收到第一段 PCM 数据的时刻 |
| `t3` | `amp_enable(true)` 之后 | 功放打开、准备播放的时刻 |

由此得到三个指标：

- `rec = t1 - t0`：录音时长（也反映用户语速和 VAD 灵敏度）
- `net = t2 - t1`：网络 + 服务端处理时间（**注意**：若上一轮正在播放，`net` 会包含等待播放完成的时间）
- `startup = t3 - t2`：功放开关和送数据的开销，本例 200 µs 量级，可忽略

**坑：**

- `ESP_LOG` 的毫秒时间戳与 `esp_timer_get_time()` 的微秒值之间有一个**固定偏移**（本例约 1.3 s），对齐时需要换算，不能直接相减
- `t0` 必须在 `xSemaphoreTake(buf_free[...])` **之前**取，否则会把等待缓冲的时间算进用户说话时间里
- 跨任务传递打点时，`t0/t1` 要随消息结构体按值传递（本例放在 `struct msg` 里），用全局变量会被下一轮覆盖
