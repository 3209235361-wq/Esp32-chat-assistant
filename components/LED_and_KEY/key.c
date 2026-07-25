#include "key.h"

bool KEY_De_trembing(gpio_num_t pin){
    if(gpio_get_level(pin)==0){
        vTaskDelay(pdMS_TO_TICKS(20));
        if(gpio_get_level(pin)==0){
            return false;
        }
        else{
            return true;
        }
    }
    return true;
}