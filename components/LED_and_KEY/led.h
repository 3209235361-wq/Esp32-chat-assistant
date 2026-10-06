#ifndef __LED_H__
#define __LED_H__

#include "driver/ledc.h"
#include "driver/gpio.h"
#include <stdbool.h>

#define PIN 7
#define LED_PWM_FREQ 1000
#define LED_PWM_TIMER 1
#define LED_PWM_CHANNEL 1

#define LED_MAX_BRIGHTNESS 255
#define LED_DEFAULT_BRIGHTNESS 200
#define LED_MIDDLE_BRIGHTNESS 128
#define LED_CLOSE 0

enum LED_STATE{
    LED_CLOSE_T,
    LED_MIDDLE_T,
    LED_DEFAULT_T,
    LED_MAX_T,
};

extern int led_state[4];
extern char *led_str_state[4];

void LED_Init(void);
void Set_Level_LED(uint32_t brightness);

#endif