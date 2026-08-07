#include "Monitor.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

void Monitor_Init(void){
    gpio_reset_pin(MOTOR_AIN1);
    gpio_set_direction(MOTOR_AIN1, GPIO_MODE_OUTPUT);
    gpio_reset_pin(MOTOR_AIN2);
    gpio_set_direction(MOTOR_AIN2, GPIO_MODE_OUTPUT);
    //Step1:Timer configuration
    ledc_timer_config_t timer_config = {
        .clk_cfg = LEDC_AUTO_CLK,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = PWM_FREQ,
        .speed_mode = LEDC_LOW_SPEED_MODE,
    };
    ledc_timer_config(&timer_config);

    //Step2:channel configuration
    ledc_channel_config_t channel_config = {
        .channel = LEDC_CHANNEL_0,
        .duty = 0,
        .gpio_num = MOTOR_PWMA,
        .timer_sel = LEDC_TIMER_0,
        .hpoint = 0,
        .speed_mode = LEDC_LOW_SPEED_MODE,
    };
    ledc_channel_config(&channel_config);
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

void motor_break(void){
    gpio_set_level(MOTOR_AIN1, 1);
    gpio_set_level(MOTOR_AIN2, 1);
    
    ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,255);
    ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0);
}