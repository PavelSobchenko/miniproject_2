#pragma once

#include <stdint.h>
#include "esp_adc/adc_oneshot.h"

#define POT_IN_MIN 0
#define POT_IN_MAX 4095

typedef struct Pot Pot;

struct Pot {
    adc_unit_t unit;
    adc_channel_t channel;
    adc_atten_t atten;
    adc_oneshot_unit_handle_t handle;
    uint16_t outMinVal;
    uint16_t outMaxVal;
};

void initPot(Pot *p);
int32_t readPot(Pot *p);