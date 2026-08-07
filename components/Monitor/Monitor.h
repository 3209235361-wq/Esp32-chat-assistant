#ifndef __MONITOR_H__
#define __MONITOR_H__

#define MOTOR_AIN1 8
#define MOTOR_AIN2 9
#define MOTOR_PWMA 10
#define PWM_FREQ 10000

void Monitor_Init(void);
void motor_set_speed(int speed);
void motor_break(void);


#endif