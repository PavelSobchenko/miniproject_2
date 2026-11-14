#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t pin;
    bool isOn;
} Led;

void initLed(Led *led);
void setLed(Led *led, bool on);
void toggleLed(Led *led);