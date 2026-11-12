#ifndef __HANDLE_CMD_H__
#define __HANDLE_CMD_H__
#include <stdbool.h>
#include "device_manager.h"
#define GEAR_TOTAL 16 

extern Node_t node_motor;
extern Node_t node_led;
extern Node_t node_volume;
extern Node_t node_voice;

typedef struct {
    const char *cmd;
    void (*func)(void);
}Cmd_t;

bool handle_cmd(char *cmd);

#endif