#include "led.h"

int led_state[4]={LED_CLOSE,LED_MIDDLE_BRIGHTNESS,LED_DEFAULT_BRIGHTNESS,LED_MAX_BRIGHTNESS};
char *led_str_state[4]={ "Close","Middle","Default","Max" };
void LED_Init(void){
    ledc_timer_config_t timer_config={
        .clk_cfg = LEDC_AUTO_CLK,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = LED_PWM_FREQ,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LED_PWM_TIMER,
    };
    ledc_timer_config(&timer_config);

    ledc_channel_config_t channel_config={
        .channel = LED_PWM_CHANNEL,
        .gpio_num = PIN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty = 0,
        .timer_sel = LED_PWM_TIMER,
        .hpoint = 0,
    };
    ledc_channel_config(&channel_config);
}

void Set_Level_LED(uint32_t brightness){
    if(brightness>LED_MAX_BRIGHTNESS){
        brightness=LED_MAX_BRIGHTNESS;
    }
    ledc_set_duty(LEDC_LOW_SPEED_MODE,LED_PWM_CHANNEL,brightness);
    ledc_update_duty(LEDC_LOW_SPEED_MODE,LED_PWM_CHANNEL);
}