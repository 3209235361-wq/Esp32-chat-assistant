#ifndef __AUDIO_H__
#define __AUDIO_H__

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

//----shared i2s bus----
#define I2S_BCLK 41  // 麦克风sck和喇叭bclk
#define I2S_LRCK 42  // 麦克风ws和喇叭lrc

//----max98357a----
#define AMP_SD 4       // shutdown (low = off, high = on)
#define DIN 5       // to MAX98357A DIN
//GAIN -> gnd=9dB

//----INMP441----
#define INMP441_SD 2       // from INMP441 SD

//----sample rate----
#define SAMPLE_RATE 16000

#define VOLUME_CLOSE 0
#define VOLUME_MIDDLE 1
#define VOLUME_DEFAULT 2
#define VOLUME_MAX 3

enum volume_state{ 
    VOLUME_CLOSE_T,
    VOLUME_MIDDLE_T,
    VOLUME_DEFAULT_T,
    VOLUME_MAX_T
};

extern char *volume_str[4];
extern int volume_state[4];

//for test
extern int voice_test[4];
extern char *voice_str_test[4];

void Audio_Init(void);
int16_t mic_read(void);
void spk_write(const int16_t *sample_data,size_t count);
void amp_enable(bool on);

#endif
