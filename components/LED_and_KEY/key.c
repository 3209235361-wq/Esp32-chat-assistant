#include "key.h"

void KEY_Init(void){
    gpio_reset_pin(KEY_PIN_UP);
    gpio_set_direction(KEY_PIN_UP, GPIO_MODE_INPUT);
    gpio_set_pull_mode(KEY_PIN_UP, GPIO_PULLUP_ONLY);
    gpio_reset_pin(KEY_PIN_DOWN);
    gpio_set_direction(KEY_PIN_DOWN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(KEY_PIN_DOWN, GPIO_PULLUP_ONLY);
    gpio_reset_pin(KEY_PIN_VERITY);
    gpio_set_direction(KEY_PIN_VERITY, GPIO_MODE_INPUT);
    gpio_set_pull_mode(KEY_PIN_VERITY, GPIO_PULLUP_ONLY);
    gpio_reset_pin(KEY_PIN_EXIT);
    gpio_set_direction(KEY_PIN_EXIT, GPIO_MODE_INPUT);
    gpio_set_pull_mode(KEY_PIN_EXIT, GPIO_PULLUP_ONLY);
}