#include "led.h"

void LED_Init(void){
    gpio_reset_pin(PIN_1);
    gpio_reset_pin(PIN_2);
    gpio_set_direction(PIN_1, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_2, GPIO_MODE_INPUT);

}
void test_LED(gpio_num_t pin,bool state){
    gpio_set_level(pin, state);
}