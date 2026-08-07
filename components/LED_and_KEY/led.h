#ifndef __LED_H__
#define __LED_H__

#include "driver/gpio.h"
#include <stdbool.h>

#define PIN 7

void LED_Init(void);
void Set_Level_LED(gpio_num_t pin,uint32_t state);

#endif