#include "led.h"

void LED_Init(void){
    gpio_reset_pin(PIN);
    gpio_set_direction(PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN, 1);//默认关闭
}

void Set_Level_LED(gpio_num_t pin,uint32_t state){
    gpio_set_level(pin, state);
}