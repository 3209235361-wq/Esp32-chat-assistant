#include "key.h"
#include "my_check.h"

const char *TAG_KEY = "key";
int key_init_err_cnt=0;

void KEY_Init(void){
    esp_err_t ret;
    ret=gpio_reset_pin(KEY_PIN_UP);
    CHECK_ERR(ret,key_init_err_cnt,TAG_KEY);
    ret=gpio_set_direction(KEY_PIN_UP, GPIO_MODE_INPUT);
    CHECK_ERR(ret,key_init_err_cnt,TAG_KEY);
    ret=gpio_set_pull_mode(KEY_PIN_UP, GPIO_PULLUP_ONLY);
    CHECK_ERR(ret,key_init_err_cnt,TAG_KEY);
    ret=gpio_reset_pin(KEY_PIN_DOWN);
    CHECK_ERR(ret,key_init_err_cnt,TAG_KEY);
    ret=gpio_set_direction(KEY_PIN_DOWN, GPIO_MODE_INPUT);
    CHECK_ERR(ret,key_init_err_cnt,TAG_KEY);
    ret=gpio_set_pull_mode(KEY_PIN_DOWN, GPIO_PULLUP_ONLY);
    CHECK_ERR(ret,key_init_err_cnt,TAG_KEY);
    ret=gpio_reset_pin(KEY_PIN_VERITY);
    CHECK_ERR(ret,key_init_err_cnt,TAG_KEY);
    ret=gpio_set_direction(KEY_PIN_VERITY, GPIO_MODE_INPUT);
    CHECK_ERR(ret,key_init_err_cnt,TAG_KEY);
    ret=gpio_set_pull_mode(KEY_PIN_VERITY, GPIO_PULLUP_ONLY);
    CHECK_ERR(ret,key_init_err_cnt,TAG_KEY);
    ret=gpio_reset_pin(KEY_PIN_EXIT);
    CHECK_ERR(ret,key_init_err_cnt,TAG_KEY);
    ret=gpio_set_direction(KEY_PIN_EXIT, GPIO_MODE_INPUT);
    CHECK_ERR(ret,key_init_err_cnt,TAG_KEY);
    ret=gpio_set_pull_mode(KEY_PIN_EXIT, GPIO_PULLUP_ONLY);
    CHECK_ERR(ret,key_init_err_cnt,TAG_KEY);
}