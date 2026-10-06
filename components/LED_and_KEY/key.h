#ifndef __KEY_H__
#define __KEY_H__

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>

#define KEY_PIN_UP 14
#define KEY_PIN_DOWN 13
#define KEY_PIN_VERITY 12
#define KEY_PIN_EXIT 11
#define KEY_DEBOUNCE_MS 20

enum key_status{
    UP=0,
    DOWN=1,
    VERITY=2,
    EXIT=3
};

void KEY_Init(void);

#endif