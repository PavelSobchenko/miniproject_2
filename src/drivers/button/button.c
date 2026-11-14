#include "button.h"
#include "driver/gpio.h"
#include "esp_attr.h"

static void IRAM_ATTR isrButtonHandler(void *arg) {
    Button *b = (Button *)arg;
    b->pending = true;
}

void initButton(Button *b) {
    gpio_install_isr_service(0);
    gpio_config_t conf = {
        .pin_bit_mask = ((1ULL << b->pin)),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = b->useInterrupt ? GPIO_INTR_NEGEDGE : GPIO_INTR_DISABLE,
    };

    gpio_config(&conf);

    if (b->useInterrupt) {
        gpio_isr_handler_add(b->pin, isrButtonHandler, b);
    }
}

ButtonState handleButtonUpdate(Button *b, uint32_t nowMs) {
    bool raw = gpio_get_level(b->pin) == BTN_ON;

    switch(b->state) {
        case BTN_STATE_IDLE:
            if (raw) {
                b->stateChangedAtMs = nowMs;
                return BTN_STATE_PRESSED;
            }
            break;
        case BTN_STATE_PRESSED:
            if (nowMs - b->stateChangedAtMs >= BTN_DEBOUNCE_MS) {
                if (!raw) {
                    b->isLongFired = false;
                    return BTN_STATE_IDLE;
                } else {
                    b->pressStartAtMs = nowMs;
                    return BTN_STATE_HELD;
                }
            }
            break;
        case BTN_STATE_HELD:
            if (!raw) {
                b->stateChangedAtMs = nowMs;
                return BTN_STATE_RELEASED;
            }
            if (!b->isLongFired && (nowMs - b->pressStartAtMs >= BTN_LONGPRESS_MS)) {
                b->isLongFired = true;
                if(b->onLongPress) b->onLongPress(b);
            }
            break;
        case BTN_STATE_RELEASED:
            if (nowMs - b->stateChangedAtMs >= BTN_DEBOUNCE_MS) {
                if (!raw) {
                    if (!b->isLongFired && b->onPress) {
                        b->onPress(b);
                    }
                    b->isLongFired = false;
                    return BTN_STATE_IDLE;
                } else {
                    return BTN_STATE_HELD;
                }
            }
            break;
    }

    return b->state;
}