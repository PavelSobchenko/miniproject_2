#include "led.h"
#include "driver/gpio.h"

void initLed(Led *led) {
    gpio_config_t conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = ((1ULL << led->pin)),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE
    };
    gpio_config(&conf);
}

void setLed(Led *led, bool on) {
    led->isOn = on;
    gpio_set_level(led->pin, on);
}

void toggleLed(Led *led) {
    setLed(led, !led->isOn);
}