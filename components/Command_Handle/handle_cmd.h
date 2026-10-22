#ifndef __HANDLE_CMD_H__
#define __HANDLE_CMD_H__
#include <stdbool.h>
#include "device_manager.h"
#include "Monitor.h"
#include "led.h"
#include <string.h>
//if you want to add more command gears for device,change it.
//but this design ideas is:each device has 4 command gears.
#define GEAR_TOTAL 8 

extern Node_t node_motor;
extern Node_t node_led;

typedef struct {
    const char *cmd;
    void (*func)(void);
}Cmd_t;

void led_to_on();
void led_to_off();
void led_to_middle();
void led_to_high();
void motor_to_on();
void motor_to_off();
void motor_to_middle();
void motor_to_high();
bool handle_cmd(char *cmd);



#endif