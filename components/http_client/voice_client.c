#include "voice_client.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_check.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


/* esp_http_client_perform(): error code mapping */
static const voice_mapping_err_t table[8]=
{
    {ESP_ERR_HTTP_CONNECT,VOICE_ERR_NET},
    {ESP_ERR_HTTP_CONNECTING,VOICE_ERR_NET},
    {ESP_ERR_HTTP_WRITE_DATA,VOICE_ERR_TIMEOUT},
    {ESP_ERR_HTTP_EAGAIN,VOICE_ERR_TIMEOUT},
    {ESP_ERR_HTTP_READ_TIMEOUT,VOICE_ERR_TIMEOUT},
    {ESP_ERR_HTTP_INCOMPLETE_DATA,VOICE_ERR_TIMEOUT},
    {ESP_ERR_HTTP_CONNECTION_CLOSED,VOICE_ERR_TIMEOUT},
    {ESP_ERR_HTTP_FETCH_HEADER,VOICE_ERR_TIMEOUT}
};
/* voice_err to string*/
static const char *str[9]=
{
    "VOICE_OK",
    "VOICE_ERR_NET",
    "VOICE_ERR_TIMEOUT",
    "VOICE_ERR_REQ",
    "VOICE_ERR_SERVER",
    "VOICE_ERR_EMPTY",
    "VOICE_ERR_OVERFLOW",
    "VOICE_ERR_UNKNOWN",
    "VOICE_ERR_LOCAL",
};

static char g_host[64] = "192.168.1.1";
static int  g_port = 8006;
static char g_user_text[256] = "";
static char g_ai_text[256]   = "";

int voice_state[4]={STD_CHINESE_FM_MODEL,STD_CHINESE_M_MODEL,CONTONESE_MODEL,ENGLISH_M_MODEL};
char *voice_str_state[4]={ STD_CHINESE_FM,STD_CHINESE_M,CONTONESE,ENGLISH_M };
static uint8_t current_voice = STD_CHINESE_FM_MODEL;

const char *TAG_CLIENT="client";

//接受指令
static char g_command[16] = "none";

// 响应体接收用的指针（在事件回调里写入）
static uint8_t *g_recv_buf = NULL;
static size_t   g_recv_max = 0;
static size_t   g_recv_total = 0;
static bool     g_recv_overflow = false;

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
//  url_decode() — 把 %XX 还原成原始字节（中文才能正常打印）
//  服务器端用 quote() 对中文做了 URL 编码，解码后是 UTF-8 字节
// ================================================================
static int _hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode(char *dst, size_t dst_max, const char *src)
{
    size_t i = 0, j = 0;
    while (src[i] && j + 1 < dst_max) {
        if (src[i] == '%' && src[i+1] && src[i+2]) {
            int hi = _hex_val(src[i+1]);
            int lo = _hex_val(src[i+2]);
            if (hi >= 0 && lo >= 0) {
                dst[j++] = (char)((hi << 4) | lo);
                i += 3;
                continue;
            }
        }
        dst[j++] = src[i++];
    }
    dst[j] = '\0';
}

// ================================================================
//  事件回调 — 捕获响应 header 和数据
// ================================================================
static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_HEADER:
        if (strcmp(evt->header_key, "x-user-text") == 0) {
            url_decode(g_user_text, sizeof(g_user_text), evt->header_value);
            printf("[HTTP] 用户: %s\n", g_user_text);
        } 
        else if (strcmp(evt->header_key, "x-ai-text") == 0) {
            url_decode(g_ai_text, sizeof(g_ai_text), evt->header_value);
            printf("[HTTP] AI: %s\n", g_ai_text);
        }
        else if(strcmp(evt->header_key,"x-command")==0){
            strncpy(g_command, evt->header_value, sizeof(g_command) - 1);
            g_command[sizeof(g_command)-1]='\0';
            printf("[HTTP] 指令: %s\n", g_command);
        }
        break;
    case HTTP_EVENT_ON_DATA:
        if (g_recv_buf && (g_recv_total + evt->data_len) <= g_recv_max) {
            memcpy(g_recv_buf + g_recv_total, evt->data, evt->data_len);
            g_recv_total += evt->data_len;
        }
        else{
            ESP_LOGW(TAG_CLIENT,"The size of received data(%u) exceeds the max size(%u).",(unsigned)(g_recv_total+evt->data_len),(unsigned)g_recv_max);
            g_recv_overflow = true;
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
Voice_Err voice_send_receive(const int16_t *pcm_in,  size_t len_in,
                              int16_t *pcm_out, size_t *len_out)
{
    Voice_Err last=VOICE_ERR_UNKNOWN;

    for(int attempt=0;attempt<2;attempt++){
        if(attempt>0){
            // buffer time for retry
            vTaskDelay(pdMS_TO_TICKS(400));
            ESP_LOGW(TAG_CLIENT, "retry %d/1, reason=%s", attempt, voice_err_to_str(last));
        }

        strncpy(g_command,"none",sizeof(g_command)-1);
        g_command[sizeof(g_command)-1]='\0';
        strncpy(g_user_text,"",sizeof(g_user_text)-1);
        g_user_text[sizeof(g_user_text)-1]='\0';
        strncpy(g_ai_text,"",sizeof(g_ai_text)-1);
        g_ai_text[sizeof(g_ai_text)-1]='\0';
        char url[128+1];
        snprintf(url, sizeof(url), "http://%s:%d/voice?sr=16000&bits=16&ch=1&voice=%d",
        g_host, g_port, current_voice);

        // 设置全局接收缓冲，事件回调会往里写数据
        g_recv_buf   = (uint8_t *)pcm_out;
        g_recv_max   = (*len_out) * sizeof(int16_t);
        g_recv_total = 0;
        g_recv_overflow = false;

        esp_http_client_config_t cfg = {
            .url            = url,
            .method         = HTTP_METHOD_POST,
            .timeout_ms     = 15000,
            .buffer_size    = 4096,
            .keep_alive_enable = true,
            .event_handler  = _http_event_handler,
        };
        /* Init */
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if(client == NULL){
            ESP_LOGE(TAG_CLIENT,"HTTP init failed");
            g_recv_buf = NULL;
            return VOICE_ERR_LOCAL;
        }

        /* Set header */
        esp_err_t result;
        result=esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
        if(result!=ESP_OK){
            ESP_LOGE(TAG_CLIENT,"HTTP set header failed result=%d",result);
            g_recv_buf = NULL;
            esp_http_client_cleanup(client);
            return VOICE_ERR_LOCAL;
        }
        
        /* Set post field */
        result=esp_http_client_set_post_field(client,
                                    (const char *)pcm_in,
                                    len_in * sizeof(int16_t));
        if(result!=ESP_OK){
            ESP_LOGE(TAG_CLIENT,"HTTP set post field failed result=%d",result);
            g_recv_buf = NULL;
            esp_http_client_cleanup(client);
            return VOICE_ERR_LOCAL;
        }

        /* Perform */
        result=esp_http_client_perform(client);
        if (result != ESP_OK) {
            int e=esp_http_client_get_errno(client);
            ESP_LOGE(TAG_CLIENT, "perform failed: %s (0x%x), errno=%d (%s)",
                 esp_err_to_name(result), result, e, strerror(e));
            g_recv_buf = NULL;
            esp_http_client_cleanup(client);
            /* travel through the voice_mapping table */
            bool try_again = false;
            for(int i=0;i<8;i++){
                if(table[i].err==result){
                    if(table[i].voice_err == VOICE_ERR_TIMEOUT){
                        try_again = true;
                        last=table[i].voice_err;
                        break;
                    }
                    else{
                        return table[i].voice_err;
                    }
                }
            }
            if(try_again) continue;
            else return VOICE_ERR_UNKNOWN;   
        }

        /* Get status code */
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG_CLIENT, "[HTTP] status: %d", status);

        esp_http_client_cleanup(client);

        // 清理全局指针，避免野指针
        g_recv_buf = NULL;

        /* tackle status code
            case 200: (ok) or (overflow | empty)
            case 204: no content
            default: 4xx | 5xx -> req | server
        */
        switch(status){
            case 200:
                if(g_recv_overflow) return VOICE_ERR_OVERFLOW;
                if(g_recv_total == 0){
                    ESP_LOGW(TAG_CLIENT,"The size of received data(%u) is 0.",(unsigned)g_recv_total);
                    return VOICE_ERR_EMPTY;
                }
                break;
            case 204:
                return VOICE_ERR_EMPTY;
            default:
                {
                    Voice_Err voice_err = (status >= 500) ? VOICE_ERR_SERVER : VOICE_ERR_REQ;
                    if(voice_err == VOICE_ERR_SERVER){
                        last=voice_err;
                        continue;
                    }
                    else{
                        return voice_err;
                    }
                }
        }

        *len_out = g_recv_total / sizeof(int16_t);
        ESP_LOGI(TAG_CLIENT, "[HTTP] recvice %u bytes PCM (%d samples)\n",
            (unsigned)g_recv_total, (int)(*len_out));
        ESP_LOGI(TAG_CLIENT, "[HTTP] total: %u overflow: %d\n", (unsigned)g_recv_total, (int)g_recv_overflow);
        return VOICE_OK;
    }
    return last;
}

void vocal_line_set(int model){
    current_voice=model;
}

const char *voice_err_to_str(Voice_Err err){
    if(err<VOICE_OK || err>VOICE_ERR_LOCAL) return "unknown error";
    return str[err];
}