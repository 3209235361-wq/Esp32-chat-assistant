#pragma once

#include <stdint.h>
#include <stddef.h>

#define SSD1306_WIDTH  128
#define SSD1306_HEIGHT 64
typedef enum str_state{
    Press = 0,
    Record = 1,
    Send = 2,
    Play = 3,
    Failed = 4,
    Empty = 5
} str_state;

typedef enum str_command{
    LED_ON = 0,
    LED_OFF = 1,
} str_command;

void ssd1306_init(uint8_t sda, uint8_t scl);
void ssd1306_fill(uint8_t color);
void ssd1306_update(void);
void ssd1306_draw_pixel(int x, int y, uint8_t color);
void ssd1306_draw_char(int x, int y, char ch);
void ssd1306_draw_string(int x, int y, const char *str);
void ssd1306_clear_row(int y);
