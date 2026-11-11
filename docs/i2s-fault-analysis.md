### 分析故障：i2s丢帧 ###

## 方式 ##

## 前提1：底层驱动不报错 ##
数据供应不上时，不返回错误码，不知道发生了卡顿。
## 前提2：卡顿不是静音，是嗡鸣声 ##
没开auto_clear，所以数据供应不上时，会继续播放最后一段旧数据，只是会嗡鸣声。
## 前提3：DMA的缓冲极限只有90ms ##
DMA 总深度 = 6 个描述符 × 240 帧 = 1440 帧 = 90 ms（16 kHz）。这就是播放时唯一的缓冲余量。

## 测量方式 ##
- 检测指标：生产者余量（producer margin）

- 思路：把"已经交给 DMA 的音频时长"和"实际流逝的时间"对比。队列里永远有 written - played 这么多数据，所以
- behind = (now - t_start) - audio_written

理想情况下，写入的音频时长往往会比实际流逝的时间多出 90ms，即 behind = -90ms。
# 正常且理想情况下，为什么behind=-90ms？
> 一个场景：
假设你按下了播放键，程序开始工作：
• 第 0 毫秒（t_start）：你的代码极其迅速，瞬间把蓄水池（DMA）灌满了，塞进去了 90ms 的音频。
	• 此时挂钟时间：0ms
	• 已塞入音频：90ms
	• 公式计算：behind = 0 - 90 = -90ms。（这就是图里说的稳态初始值）
• 挂钟走到 10 毫秒（now）：
	• 这 10ms 里，水龙头流走了 10ms 的水，水池里还剩 80ms。
	• 你的代码（送水工）非常勤快，立刻又往水池里补了 10ms 的水。水池又满了。
	• 此时挂钟时间：10ms
	• 已塞入音频：90 + 10 = 100ms
	• 公式计算：behind = 10 - 100 = -90ms。
• 挂钟走到 50 毫秒（now）：
	• 你的代码继续无缝衔接，每次都立刻补水。
	• 此时挂钟时间：50ms
	• 已塞入音频：90 + 50 = 140ms
	• 公式计算：behind = 50 - 140 = -90ms。 
💡 深度理解：
只要你的 CPU 供数据的速度跟得上，audio_written 永远比真实流逝的时间多出 90ms（因为你提前预支了 90ms 把池子填满了）。这个 -90ms 代表什么？它代表的是你的“安全库存”。 只要 behind 是负数，说明蓄水池里是有水的，播放极其稳定。 

# 卡顿情况下的 behind 值 #
>一个场景：
现在，假设你的程序突然遇到了一段极其复杂的计算（比如正在解析网络数据、或者访问 SD 卡），CPU 卡住了 100 毫秒，在这 100ms 内，你的代码没有调用 i2s_channel_write。
• 挂钟从 50ms 卡到了 100ms（过了 50ms）：
	• 水龙头一直在放水，把水池里原本存着的 90ms 水放走了一部分。
	• 你的代码没干活，没塞新水。
	• 此时挂钟时间：100ms
	• 已塞入音频：140ms（还是刚才那点）
	• 公式计算：behind = 100 - 140 = -40ms。
	• (状态预警：余量只剩 40ms 了，水池快见底了！)
• 挂钟走到了 140ms：
	• 此时蓄水池里最后 1 滴水被吸干。
	• 挂钟时间：140ms
	• 已塞入音频：140ms
	• 公式计算：behind = 140 - 140 = 0ms。
	• (极其关键点：此刻水池刚好空！)
• 挂钟走到了 150ms（灾难发生）：
	• 真实时间过去了 150ms，但是因为没有新数据，I2S 硬件被迫开始重复播放之前的数据（也就是说的“嗡鸣声”）。
	• 挂钟时间：150ms
	• 已塞入音频：140ms
	• 公式计算：behind = 150 - 140 = 10ms。
	• (结果：behind 变成正数了！) 
💡 深度理解：
• 当 behind < 0（比如 -90 到 -1 之间）：系统是安全的。-behind 就是你的余量。
• 当 behind = 0：警报拉响，水池干了，马上要卡顿了。
• 当 behind > 0：卡顿已经发生了。数值是 10ms，意味着你已经饿了硬件 10 毫秒了（这 10ms 里播放的都是杂音）。

## 代码 ##
```c
void spk_write(const int16_t *sample_data,size_t count){
    //i2s stereo的数据格式是：l0 r0 l1 r1 ...
    //虽然只写左声道，但是为了保持双声道格式，需要填充右声道数据
    size_t written_bytes=0;
    int16_t stereo[512];//buffer for stereo data

    //----- fault analysis --------
    int64_t t_start=esp_timer_get_time();
    int64_t audio_us=0;
    int64_t max_behind=-90000,behind=0;   // 初值取 -90 ms：一块都没更新时不会误报欠载
    int64_t worst_at_us=0;
    //----- fault analysis --------

    for(size_t i=0;i<count;i+=256){//One round handles 256 datas.

        //----- fault analysis --------
        behind=(esp_timer_get_time()-t_start)-audio_us;
        //skip: first round,just fill time,not represent the state of queue
        if(i != 0 && behind>max_behind){
            max_behind=behind;
            worst_at_us=esp_timer_get_time()-t_start;   // 峰值时刻与峰值绑在同一分支
        }
        //----- fault analysis --------

        size_t n=(i+256<=count)?256:(count-i);
        for(int j=0;j<n;j++){
            stereo[j*2]=(sample_data[i+j]*current_volume)/VOLUME_MAX;
            stereo[j*2+1]=(sample_data[i+j]*current_volume)/VOLUME_MAX;
        }
        i2s_channel_write(tx_handle,stereo,sizeof(int16_t)*n*2,&written_bytes,portMAX_DELAY);

        //----- fault analysis --------
        audio_us+=n*1000000/SAMPLE_RATE; //s->us
        //----- fault analysis --------
    }
    //----- fault analysis --------
    // 一轮只打一行：逐块打日志本身会占掉 16 ms 块预算的可观比例，测量会干扰被测对象
    if(max_behind>=0){
        ESP_LOGW(TAG_AUDIO,"[i2s] play=%llds worst_behind= %lld ms , at %lld ms underrun.",
            audio_us/1000000,max_behind/1000,worst_at_us/1000);
    }
    else{
        ESP_LOGI(TAG_AUDIO,"[i2s] play=%llds worst_behind= %lld ms , not underrun.",
            audio_us/1000000,max_behind/1000);
    }
    //----- fault analysis --------
}
```

## 实测结果 ##

| 场景 | 回复时长 | worst_behind | 结果 |
|---|---|---|---|
| 正常播放 | 5.35 s | -15 ms | 无欠载 |
| 注入 200 ms 抢占（`i==4096` 处 `vTaskDelay`）| 2.45 s | +135 ms @0.39 s | 欠载，人耳听到重复音节 |

两点和上面的理想模型有关：

- **-90 ms 是理论上限，实测的最好值只有 -15 ms。** 队列深度不是恒定值：DMA 在持续放水、生产端按 16 ms 一块补水，采样点落在一次补水与下一次补水之间，读到的余量就在 15 ~ 90 ms 之间摆动。`worst_behind` 记录的是**一轮里最贴近 0 的那次采样**，所以 -15 ms 的含义是"最险的时刻仍有 15 ms 余量"。
- **注入 200 ms 后只到 +135 ms 而不是 +200。** 202 − 135 = 65 ms，说明卡顿发生的那一瞬间队列里还有约 65 ms 的货，先被吃掉，剩下的 135 ms 才是真正的饥饿量。

结论：本项目的播放链路在正常负载下不会丢帧；一旦 `worst_behind` 转正，就说明这一轮出现过欠载，人耳听到的是旧数据重放的重复音节。