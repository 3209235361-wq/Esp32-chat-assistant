#ifndef __CHECK_ERR_H__
#define __CHECK_ERR_H__
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
/*
    CHECK_ERR: just check the function which return void type
    CHECK_POINTER: just like CHECK_ERR
    INIT_CHECK_ERR: Public function
*/
#define CHECK_ERR(ret, cnt , TAG) \
    do{\
        if((ret)!=ESP_OK){\
            (cnt)+=1;\
            ESP_RETURN_VOID_ON_ERROR(ret, TAG, "init failed: %s", esp_err_to_name(ret));\
        }\
    }while(0) \

#define CHECK_POINTER(pointer, cnt , TAG) \
    do{\
        if((pointer)==NULL){\
            (cnt)+=1;\
            ESP_RETURN_VOID_ON_FALSE(pointer, TAG, "init failed: pointer is NULL");\
        }\
    }while(0) \

#define INIT_CHECK_ERR(err_cnt, TAG)         \
    do{                                 \
        if((err_cnt) !=0 ){             \
            ESP_LOGE(TAG, "init failed: %d", (err_cnt)); \
            while(1){vTaskDelay(pdMS_TO_TICKS(1000));} \
        }                               \
    }while(0)\

#endif