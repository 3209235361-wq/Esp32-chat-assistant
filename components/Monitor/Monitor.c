#include "Monitor.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "my_check.h"

const char *TAG_MONITOR = "monitor";
int monitor_init_err_cnt = 0;
int motor_state[4]={MOTOR_CLOSE,MIDDLE_SPEED,DEFAULT_SPEED,MAX_SPEED};
char *motor_str_state[4]={ "Close","Middle","Default","Max" };

void Monitor_Init(void){
    esp_err_t ret;
    ret = gpio_reset_pin(MOTOR_AIN1);
    CHECK_ERR(ret,monitor_init_err_cnt,TAG_MONITOR);
    ret = gpio_set_direction(MOTOR_AIN1, GPIO_MODE_OUTPUT);
    CHECK_ERR(ret,monitor_init_err_cnt,TAG_MONITOR);
    ret = gpio_reset_pin(MOTOR_AIN2);
    CHECK_ERR(ret,monitor_init_err_cnt,TAG_MONITOR);
    ret = gpio_set_direction(MOTOR_AIN2, GPIO_MODE_OUTPUT);
    CHECK_ERR(ret,monitor_init_err_cnt,TAG_MONITOR);
    //Step1:Timer configuration
    ledc_timer_config_t timer_config = {
        .clk_cfg = LEDC_AUTO_CLK,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = MOTOR_PWM_FREQ,
        .speed_mode = LEDC_LOW_SPEED_MODE,
    };
    ret = ledc_timer_config(&timer_config);
    CHECK_ERR(ret,monitor_init_err_cnt,TAG_MONITOR);

    //Step2:channel configuration
    ledc_channel_config_t channel_config = {
        .channel = LEDC_CHANNEL_0,
        .duty = 0,
        .gpio_num = MOTOR_PWMA,
        .timer_sel = LEDC_TIMER_0,
        .hpoint = 0,
        .speed_mode = LEDC_LOW_SPEED_MODE,
    };
    ret = ledc_channel_config(&channel_config);
    CHECK_ERR(ret,monitor_init_err_cnt,TAG_MONITOR);
}

void motor_set_speed(int speed){
    if(speed>255){speed=255;}
    if(speed<-255){speed=-255;}

    gpio_set_level(MOTOR_AIN1, speed>0);//正
    gpio_set_level(MOTOR_AIN2, speed<0);//反

    int duty = (speed>0)?speed:-speed;
    ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0);
}
