#include "handle_cmd.h"

Cmd_t cmd_gear[GEAR_TOTAL]={
    {"led_on",led_to_on},
    {"led_off",led_to_off},
    {"led_middle",led_to_middle},
    {"led_high",led_to_high},
    {"motor_on",motor_to_on},
    {"motor_off",motor_to_off},
    {"motor_middle",motor_to_middle},
    {"motor_high",motor_to_high},
};

void led_to_on(){
    Set_Level_LED(LED_DEFAULT_BRIGHTNESS);
    node_led.state=LED_DEFAULT_T;
}
void led_to_off(){
    Set_Level_LED(LED_CLOSE);
    node_led.state=LED_CLOSE_T;
}
void led_to_middle(){
    Set_Level_LED(LED_MIDDLE_BRIGHTNESS);
    node_led.state=LED_MIDDLE_T;
}
void led_to_high(){
    Set_Level_LED(LED_MAX_BRIGHTNESS);
    node_led.state=LED_MAX_T;
}
void motor_to_on(){
    motor_set_speed(DEFAULT_SPEED);
    node_motor.state=DEFAULT_SPEED_T;
}
void motor_to_off(){
    motor_set_speed(MOTOR_CLOSE);
    node_motor.state=MOTOR_CLOSE_T;
}
void motor_to_middle(){
    motor_set_speed(MIDDLE_SPEED);
    node_motor.state=MIDDLE_SPEED_T;
}
void motor_to_high(){
    motor_set_speed(MAX_SPEED);
    node_motor.state=MAX_SPEED_T;
}

bool handle_cmd(char *cmd){
    for(int i=0;i<GEAR_TOTAL;i++){
        if(strcmp(cmd,cmd_gear[i].cmd)==0){
            cmd_gear[i].func();
            return true;
        }
    }
    return false;
}