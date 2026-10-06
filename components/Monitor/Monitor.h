#ifndef __MONITOR_H__
#define __MONITOR_H__

#define MOTOR_AIN1 8
#define MOTOR_AIN2 9
#define MOTOR_PWMA 10
#define MOTOR_PWM_FREQ 10000

#define MAX_SPEED 255
#define DEFAULT_SPEED 200
#define MIDDLE_SPEED 128
#define MOTOR_CLOSE 0

enum MOTOR_STATE{
    MOTOR_CLOSE_T,
    MIDDLE_SPEED_T,
    DEFAULT_SPEED_T,
    MAX_SPEED_T,
};

extern int motor_state[4];
extern char *motor_str_state[4];
void Monitor_Init(void);
void motor_set_speed(int speed);
void motor_break(void);


#endif