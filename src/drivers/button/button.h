#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BTN_ON 0
#define BTN_OFF 1
#define BTN_DEBOUNCE_MS 50
#define BTN_LONGPRESS_MS 2000

typedef enum {
    BTN_STATE_IDLE,
    BTN_STATE_PRESSED,
    BTN_STATE_HELD,
    BTN_STATE_RELEASED,
} ButtonState;

typedef struct Button Button;
typedef void (*ButtonCallback)(Button *b);

struct Button {
    // common fields
    bool useInterrupt;
    volatile bool pending;
    ButtonState state;
    uint8_t pin;

    // callbacks
    ButtonCallback onPress;
    ButtonCallback onLongPress;
    void *ctx;

    // polling fields
    bool isLongFired;
    uint32_t stateChangedAtMs;
    uint32_t pressStartAtMs;
};

void initButton(Button *b);
ButtonState handleButtonUpdate(Button *b, uint32_t nowMs);