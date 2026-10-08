#include "ssd1306.h"
#include "ssd1306_font.h"
#include "driver/i2c_master.h"
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <assert.h>

#define I2C_ADDR  0x3C
#define I2C_FREQ  400000

SemaphoreHandle_t ssd1306_semaphore = NULL;

static i2c_master_dev_handle_t dev_handle;
static uint8_t framebuffer[SSD1306_WIDTH * SSD1306_HEIGHT / 8];

static const uint8_t init_cmds[] = {
    0xAE,         // display off
    0xD5, 0x80,   // clock div
    0xA8, 0x3F,   // mux ratio �? 64
    0xD3, 0x00,   // display offset
    0x40,         // start line
    0x8D, 0x14,   // charge pump
    0x20, 0x00,   // horizontal addressing
    0xA1,         // segment remap
    0xC8,         // COM scan direction
    0xDA, 0x12,   // COM pins
    0x81, 0xCF,   // contrast
    0xD9, 0xF1,   // precharge
    0xDB, 0x40,   // VCOM detect
    0xA4,         // resume to RAM content
    0xA6,         // normal (not inverted)
    0xAF,         // display on
};

static void ssd1306_write_cmd(const uint8_t *data, size_t len)
{
    i2c_master_transmit(dev_handle, data, len, -1);
}

void ssd1306_init(uint8_t sda, uint8_t scl)
{
    // 必须在任何 ssd1306_xxx 调用之前创建：这些函数内部都会 take 这把锁
    ssd1306_semaphore = xSemaphoreCreateRecursiveMutex();
    assert(ssd1306_semaphore);

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus_handle;
    i2c_new_master_bus(&bus_cfg, &bus_handle);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = I2C_ADDR,
        .scl_speed_hz = I2C_FREQ,
    };
    i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);

    // Send init commands with 0x00 prefix for each byte
    uint8_t buf[2];
    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        buf[0] = 0x00;
        buf[1] = init_cmds[i];
        ssd1306_write_cmd(buf, 2);
    }

    ssd1306_fill(0);
    ssd1306_update();
}

void ssd1306_fill(uint8_t color)
{
    xSemaphoreTakeRecursive(ssd1306_semaphore, portMAX_DELAY);
    memset(framebuffer, color ? 0xFF : 0x00, sizeof(framebuffer));
    xSemaphoreGiveRecursive(ssd1306_semaphore);
}

void ssd1306_draw_pixel(int x, int y, uint8_t color)
{
    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT) return;
    xSemaphoreTakeRecursive(ssd1306_semaphore, portMAX_DELAY);
    int idx = x + (y / 8) * SSD1306_WIDTH;
    if (color)
        framebuffer[idx] |= (1 << (y & 7));
    else
        framebuffer[idx] &= ~(1 << (y & 7));
    xSemaphoreGiveRecursive(ssd1306_semaphore);
}

void ssd1306_draw_char(int x, int y, char ch)
{
    if (ch < ' ' || ch > '~') ch = ' ';
    int idx = ch - ' ';
    xSemaphoreTakeRecursive(ssd1306_semaphore, portMAX_DELAY);
    for (int col = 0; col < 6; col++) {
        uint8_t line = font6x8[idx][col];
        for (int row = 0; row < 8; row++) {
            if (line & (1 << row))
                ssd1306_draw_pixel(x + col, y + row, 1);
        }
    }
    xSemaphoreGiveRecursive(ssd1306_semaphore);
}

void ssd1306_draw_string(int x, int y, const char *str)
{
    while (*str) {
        ssd1306_draw_char(x, y, *str++);
        x += 6;
        if (x > SSD1306_WIDTH - 6) {
            x = 0;
            y += 8;
        }
    }
}

void ssd1306_clear_row(int y)
{
    if (y < 0 || y >= SSD1306_HEIGHT) return;
    int page = y / 8;
    xSemaphoreTakeRecursive(ssd1306_semaphore, portMAX_DELAY);
    for (int x = 0; x < SSD1306_WIDTH; x++) {
        framebuffer[x + page * SSD1306_WIDTH] = 0;
    }
    // 如果文字跨了两个 page，清第二个
    if (y % 8 != 0 && page + 1 < 8) {
        for (int x = 0; x < SSD1306_WIDTH; x++) {
            framebuffer[x + (page + 1) * SSD1306_WIDTH] = 0;
        }
    }
    xSemaphoreGiveRecursive(ssd1306_semaphore);
}

void ssd1306_update(void)
{
    xSemaphoreTakeRecursive(ssd1306_semaphore, portMAX_DELAY);
    for (int page = 0; page < 8; page++) {
        uint8_t cmd[] = {
            0x00, 0xB0 + page,   // set page
            0x00, 0x00,          // set column low
            0x00, 0x10,          // set column high
        };
        ssd1306_write_cmd(cmd, sizeof(cmd));

        uint8_t data[SSD1306_WIDTH + 1];
        data[0] = 0x40;
        memcpy(data + 1, framebuffer + page * SSD1306_WIDTH, SSD1306_WIDTH);
        ssd1306_write_cmd(data, sizeof(data));
    }
    xSemaphoreGiveRecursive(ssd1306_semaphore);
}



void show_outside(Header_t *header,mList_t *list){
    ssd1306_fill(0);
    for(int i=0;i<128;i++){
        ssd1306_draw_pixel(i,0,SSD1306_WHITE);
        ssd1306_draw_pixel(i,63,SSD1306_WHITE);
    }
    for(int k=0;k<64;k++){
        ssd1306_draw_pixel(0,k,SSD1306_WHITE);
        ssd1306_draw_pixel(127,k,SSD1306_WHITE);
    }
    ssd1306_draw_string(8,1*8,"WiFi:");
    ssd1306_draw_string(36,1*8,wifi_str_state[WiFi_isConnect()]);
    int original_pos=ORIGINAL_POS;
    for(mList_t *pos=header->head.next;pos!=&header->head;pos=pos->next){
        Node_t *node = container_of(pos,Node_t,list);
        ssd1306_draw_string(28,original_pos*8,node->label);
        if(list==&node->list){
            ssd1306_draw_string(16,original_pos*8,"-");
        }
        original_pos++;
    }
}

void show_inside(Node_t *node){
    ssd1306_fill(0);
    for(int i=0;i<128;i++){
        ssd1306_draw_pixel(i,0,SSD1306_WHITE);
        ssd1306_draw_pixel(i,63,SSD1306_WHITE);
    }
    for(int k=0;k<64;k++){
        ssd1306_draw_pixel(0,k,SSD1306_WHITE);
        ssd1306_draw_pixel(127,k,SSD1306_WHITE);
    }
    ssd1306_draw_string(24,2*8,"Device:");
    ssd1306_draw_string(66,2*8,node->name);
    ssd1306_draw_string(24,5*8,"State:");
    ssd1306_draw_string(66,5*8,node->state_table[node->state]);
}