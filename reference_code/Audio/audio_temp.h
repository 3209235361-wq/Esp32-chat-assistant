#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ---------- Shared I2S bus ----------
// BCLK and LRCK are shared by both mic and amp
#define I2S_BCLK    41
#define I2S_LRCK    42
#define I2S_DIN     2       // from INMP441 SD
#define I2S_DOUT    5       // to MAX98357A DIN

// ---------- MAX98357A control ----------
#define AMP_SD      4       // shutdown (low = off, high = on)

// ---------- MAX98357A pinout reference ----------
// VIN  -> 3.3V ~ 5V
// GND  -> GND
// BCLK -> I2S_BCLK (shared)
// LRC  -> I2S_LRCK (shared)
// DIN  -> I2S_DOUT
// SD   -> AMP_SD  (high = enabled)
// GAIN -> float=9dB, GND=3dB, VCC=15dB

#define SAMPLE_RATE 16000

void audio_init(void);
int16_t mic_read_sample(void);
void spk_write(const int16_t *samples, size_t count);
void amp_enable(bool on);
