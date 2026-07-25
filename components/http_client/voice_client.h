#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ---- 发送录音，接收 AI 回复 ----
// pcm_in:   录制的 PCM 数据 (int16_t 数组)
// len_in:   采样点个数
// pcm_out:  输出的 PCM 缓冲区（调用者分配好空间）
// len_out:  输入时 = 缓冲区最大采样点数，输出时 = 实际填充的采样点数
// 返回:     true=成功, false=失败
bool voice_send_receive(const int16_t *pcm_in,  size_t len_in,
                              int16_t *pcm_out, size_t *len_out);

// ---- 设置服务器地址（初始化时调一次）----
void voice_set_server(const char *host, int port);

// ---- 获取最后一次的识别和回复文字（给 OLED 显示）----
const char *voice_last_user_text(void);
const char *voice_last_ai_text(void);
