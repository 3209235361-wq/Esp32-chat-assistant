#pragma once

#include <stdint.h>
#include <stddef.h>
#include "device_manager.h"
#include "wifi.h"

#define SSD1306_WIDTH  128
#define SSD1306_HEIGHT 64

enum ssd1306_color{
    SSD1306_BLACK = 0,
    SSD1306_WHITE = 1,
};

void ssd1306_init(uint8_t sda, uint8_t scl);
void ssd1306_fill(uint8_t color);
void ssd1306_update(void);
void ssd1306_draw_pixel(int x, int y, uint8_t color);
void ssd1306_draw_char(int x, int y, char ch);
void ssd1306_draw_string(int x, int y, const char *str);
void ssd1306_clear_row(int y);
void show_outside(Header_t *header,mList_t *list);
void show_inside(Node_t *node);

