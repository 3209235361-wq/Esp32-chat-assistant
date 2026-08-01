#include "key.h"

void KEY_Init(void){
    gpio_reset_pin(KEY_PIN);
    gpio_set_direction(KEY_PIN, GPIO_MODE_INPUT);
}



key_state_t KEY_De_trembing(gpio_num_t pin){
    if(gpio_get_level(pin)==0){
        vTaskDelay(pdMS_TO_TICKS(20));
        if(gpio_get_level(pin)==0){
            return KEY_PRESSED;
        }
        else{
            return KEY_RELEASED;
        }
    }
    return KEY_NOT_PRESSED;
}