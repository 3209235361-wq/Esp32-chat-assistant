#ifndef __LED_H__
#define __LED_H__

#include "driver/gpio.h"
#include <stdbool.h>

#define PIN_1 16
#define PIN_2 18
void LED_Init(void);
void test_LED(gpio_num_t pin,bool state);

#endif