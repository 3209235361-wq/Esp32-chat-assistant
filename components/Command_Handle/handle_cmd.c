#include "handle_cmd.h"
#include "led.h"
#include "Monitor.h"
#include "audio.h"
#include "voice_client.h"
#include <string.h>

static void led_to_on();
static void led_to_off();
static void led_to_middle();
static void led_to_high();
static void motor_to_on();
static void motor_to_off();
static void motor_to_middle();
static void motor_to_high();
static void volume_to_high();
static void volume_to_default();
static void volume_to_middle();
static void volume_to_close();
static void voice_to_female();
static void voice_to_male();
static void voice_to_hk();
static void voice_to_en();

static Cmd_t cmd_gear[GEAR_TOTAL]={
    {"led_on",led_to_on},
    {"led_off",led_to_off},
    {"led_middle",led_to_middle},
    {"led_high",led_to_high},
    {"motor_on",motor_to_on},
    {"motor_off",motor_to_off},
    {"motor_middle",motor_to_middle},
    {"motor_high",motor_to_high},
    {"volume_high",volume_to_high},
    {"volume_default",volume_to_default},
    {"volume_middle",volume_to_middle},
    {"volume_close",volume_to_close},
    {"voice_female",voice_to_female},
    {"voice_male",voice_to_male},
    {"voice_hk",voice_to_hk},
    {"voice_en_us",voice_to_en},
};

//--- led ---
static void led_to_on(){
    Set_Level_LED(LED_DEFAULT_BRIGHTNESS);
    node_led.state=LED_DEFAULT_T;
}
static void led_to_off(){
    Set_Level_LED(LED_CLOSE);
    node_led.state=LED_CLOSE_T;
}
static void led_to_middle(){
    Set_Level_LED(LED_MIDDLE_BRIGHTNESS);
    node_led.state=LED_MIDDLE_T;
}
static void led_to_high(){
    Set_Level_LED(LED_MAX_BRIGHTNESS);
    node_led.state=LED_MAX_T;
}
//--- motor ---
static void motor_to_on(){
    motor_set_speed(DEFAULT_SPEED);
    node_motor.state=DEFAULT_SPEED_T;
}
static void motor_to_off(){
    motor_set_speed(MOTOR_CLOSE);
    node_motor.state=MOTOR_CLOSE_T;
}
static void motor_to_middle(){
    motor_set_speed(MIDDLE_SPEED);
    node_motor.state=MIDDLE_SPEED_T;
}
static void motor_to_high(){
    motor_set_speed(MAX_SPEED);
    node_motor.state=MAX_SPEED_T;
}

//--- volume ---
static void volume_to_high(){
    spk_set_volume(VOLUME_MAX);
    node_volume.state=VOLUME_MAX_T;
}

static void volume_to_default(){
    spk_set_volume(VOLUME_DEFAULT);
    node_volume.state=VOLUME_DEFAULT_T;
}

static void volume_to_middle(){
    spk_set_volume(VOLUME_MIDDLE);
    node_volume.state=VOLUME_MIDDLE_T;
}

static void volume_to_close(){
    spk_set_volume(VOLUME_CLOSE);
    node_volume.state=VOLUME_CLOSE_T;
}

//--- voice ---
static void voice_to_female(){
    vocal_line_set(STD_CHINESE_FM_MODEL);
    node_voice.state=STD_CHINESE_FM_MODEL;
}

static void voice_to_male(){
    vocal_line_set(STD_CHINESE_M_MODEL);
    node_voice.state=STD_CHINESE_M_MODEL;
}

static void voice_to_hk(){
    vocal_line_set(CONTONESE_MODEL);
    node_voice.state=CONTONESE_MODEL;
}

static void voice_to_en(){
    vocal_line_set(ENGLISH_M_MODEL);
    node_voice.state=ENGLISH_M_MODEL;
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