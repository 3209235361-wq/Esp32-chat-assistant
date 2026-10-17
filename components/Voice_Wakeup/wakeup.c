#include "wakeup.h"
#include "esp_check.h"

const char *TAG_WAKEUP="wakeup";

esp_afe_sr_data_t *afe_data = NULL;
const esp_afe_sr_iface_t *afe_handle = NULL;

void WakeEngine_Init(void){
    srmodel_list_t *model=esp_srmodel_init("model");
    ESP_RETURN_VOID_ON_FALSE(model != NULL, TAG_WAKEUP, "model load failed");

    char *wn_name = esp_srmodel_filter(model,ESP_WN_PREFIX,NULL);
    ESP_RETURN_VOID_ON_FALSE(wn_name != NULL, TAG_WAKEUP, "No,wn model,check SDK menuconfig");
    ESP_LOGI(TAG_WAKEUP,"wn model:%s",wn_name);

    //创建临时配置结构体
    afe_config_t *cfg=afe_config_init("M",model,AFE_TYPE_SR,AFE_MODE_LOW_COST);
    ESP_RETURN_VOID_ON_FALSE(cfg != NULL, TAG_WAKEUP, "afe_config_init failed");

    cfg->wakenet_model_name=wn_name;
    cfg->wakenet_init = true;
    cfg->wakenet_mode = DET_MODE_90;

    cfg->vad_init = true;
    cfg->vad_mode = VAD_MODE_3;//静音检测严格度
    cfg->vad_min_speech_ms = 128;//最短说话时间
    cfg->vad_min_noise_ms = 500;//结束判定时间

    //神经推理内存分配到PSRAM，flash内存不足
    cfg->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;

    afe_handle = esp_afe_handle_from_config(cfg);
    ESP_RETURN_VOID_ON_FALSE(afe_handle != NULL, TAG_WAKEUP, "esp_afe_handle_from_config failed");
    afe_data = afe_handle->create_from_config(cfg);
    ESP_RETURN_VOID_ON_FALSE(afe_data != NULL, TAG_WAKEUP, "create_from_config failed");

    ESP_LOGI(TAG_WAKEUP,"feed=%d fetch=%d",
        afe_handle->get_feed_chunksize(afe_data),
        afe_handle->get_fetch_chunksize(afe_data));

    free(cfg);//释放临时配置结构体
}

