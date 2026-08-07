#ifndef __KEY_H__
#define __KEY_H__

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>

#define KEY_PIN 15

typedef enum {
    KEY_NOT_PRESSED,
    KEY_PRESSED,
    KEY_RELEASED
} key_state_t;

void KEY_Init(void);
key_state_t KEY_De_trembing(gpio_num_t pin);


#endif