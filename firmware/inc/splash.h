#pragma once

#include <stdint.h>

#define SPLASH_SCREEN_WIDTH  32
#define SPLASH_SCREEN_HEIGHT 128
#define SPLASH_SCREEN_COUNT  5
#define SPLASH_BITMAP_BYTES  ((SPLASH_SCREEN_WIDTH / 8) * SPLASH_SCREEN_HEIGHT)

extern const uint8_t splash_flame1[SPLASH_BITMAP_BYTES];
extern const uint8_t splash_flame2[SPLASH_BITMAP_BYTES];
extern const uint8_t splash_pcb1[SPLASH_BITMAP_BYTES];
extern const uint8_t splash_pcb2[SPLASH_BITMAP_BYTES];
extern const uint8_t splash_spark1[SPLASH_BITMAP_BYTES];

extern const uint8_t* const splash_screens[SPLASH_SCREEN_COUNT];
