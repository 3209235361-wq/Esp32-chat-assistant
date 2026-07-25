#ifndef __AUDIO_H__
#define __AUDIO_H__

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

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

void Audio_Init(void);
int16_t mic_read(void);
void spk_write(const int16_t *sample_data,size_t count);
void amp_enable(bool on);

#endif
