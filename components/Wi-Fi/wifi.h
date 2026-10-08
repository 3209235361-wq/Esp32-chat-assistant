#ifndef __WIFI_H__
#define __WIFI_H__

#include <stdbool.h>
extern char *wifi_str_state[2];
extern void (*wifi_state_cb)(void);
void WiFi_Connect(const char *ssid, const char *password);
bool WiFi_isConnect(void);

#endif