#ifndef __WAKEUP_H__
#define __WAKEUP_H__
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "esp_afe_sr_models.h"     // esp_afe_sr_1mic 单麦接口
#include "esp_wn_models.h"         // 唤醒词模型名（如 "wn9"）
#include "esp_afe_sr_iface.h"
#include "esp_afe_config.h"
#include "model_path.h"

void WakeEngine_Init(void);

#endif