#include "voice_client.h"
#include "esp_http_client.h"
#include <string.h>
#include <stdio.h>

static char g_host[64] = "192.168.1.1";
static int  g_port = 8006;
static char g_user_text[256] = "";
static char g_ai_text[256]   = "";

//接受指令
static char g_command[16] = "none";

// 响应体接收用的指针（在事件回调里写入）
static uint8_t *g_recv_buf = NULL;
static size_t   g_recv_max = 0;
static size_t   g_recv_total = 0;

// ================================================================
//  voice_set_server() — 设置后端地址
// ================================================================
void voice_set_server(const char *host, int port)
{
    strncpy(g_host, host, sizeof(g_host) - 1);
    g_port = port;
}

const char *voice_last_user_text(void) { return g_user_text; }
const char *voice_last_ai_text(void)   { return g_ai_text;   }
const char *voice_last_command(void)   { return g_command;   }

// ================================================================
//  事件回调 — 捕获响应 header 和数据
// ================================================================
static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_HEADER:
        if (strcmp(evt->header_key, "x-user-text") == 0) {
            strncpy(g_user_text, evt->header_value, sizeof(g_user_text) - 1);
            printf("[HTTP] 用户: %s\n", g_user_text);
        } else if (strcmp(evt->header_key, "x-ai-text") == 0) {
            strncpy(g_ai_text, evt->header_value, sizeof(g_ai_text) - 1);
            printf("[HTTP] AI: %s\n", g_ai_text);
        }
        else if(strcmp(evt->header_key,"x-command")==0){
            strncpy(g_command, evt->header_value, sizeof(g_command) - 1);
            printf("[HTTP] 指令: %s\n", g_command);
        }
        break;
    case HTTP_EVENT_ON_DATA:
        if (g_recv_buf && (g_recv_total + evt->data_len) <= g_recv_max) {
            memcpy(g_recv_buf + g_recv_total, evt->data, evt->data_len);
            g_recv_total += evt->data_len;
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

// ================================================================
//  voice_send_receive() — POST 录音 → 收 PCM 回复
// ================================================================
bool voice_send_receive(const int16_t *pcm_in,  size_t len_in,
                              int16_t *pcm_out, size_t *len_out)
{
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/voice?sr=16000&bits=16&ch=1",
             g_host, g_port);

    // 设置全局接收缓冲，事件回调会往里写数据
    g_recv_buf   = (uint8_t *)pcm_out;
    g_recv_max   = (*len_out) * sizeof(int16_t);
    g_recv_total = 0;

    esp_http_client_config_t cfg = {
        .url            = url,
        .method         = HTTP_METHOD_POST,
        .timeout_ms     = 30000,
        .buffer_size    = 4096,
        .keep_alive_enable = true,
        .event_handler  = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);

    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    esp_http_client_set_post_field(client,
                                   (const char *)pcm_in,
                                   len_in * sizeof(int16_t));

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        printf("[HTTP] 请求失败: %d\n", err);
        esp_http_client_cleanup(client);
        return false;
    }

    int status = esp_http_client_get_status_code(client);
    printf("[HTTP] status: %d\n", status);

    esp_http_client_cleanup(client);

    // 清理全局指针，避免野指针
    g_recv_buf = NULL;

    if (status != 200) return false;

    *len_out = g_recv_total / sizeof(int16_t);
    printf("[HTTP] 收到 %u bytes PCM (%d samples)\n",
           (unsigned)g_recv_total, (int)(*len_out));
    return (*len_out > 0);
}
