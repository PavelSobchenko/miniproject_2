#include "buzzer.h"

static uint32_t getNoteDurationMs(const Note *n, uint16_t bpm) {
    uint32_t whole = 60000UL * 4 / bpm;
    int8_t d = n->div < 0 ? -n->div : n->div;
    uint32_t dur = whole / d;
    if (n->div < 0) dur += dur / 2;
    return dur;
}
static void startNote(Buzzer *b, uint32_t nowMs) {
    const Note *n = &b->melody->notes[b->index];
    uint32_t dur = getNoteDurationMs(n, b->melody->bpm);

    toneBuzzer(b, n->freq);
    b->inGap = false;
    b->gapMs = dur / 10;
    b->nextChangeMs = nowMs + dur - b->gapMs;
}

void initBuzzer(Buzzer *b) {
    ledc_timer_config_t t = {
        .speed_mode      = b->mode,
        .timer_num       = b->timer,
        .duty_resolution = b->res,
        .freq_hz         = 1000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&t));

    ledc_channel_config_t c = {
        .speed_mode = b->mode,
        .channel    = b->channel,
        .timer_sel  = b->timer,
        .gpio_num   = b->pin,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}

void toneBuzzer(Buzzer *b, uint32_t freq_hz) {
    if (freq_hz == 0) {
        offBuzzer(b);
        return;
    }
    ledc_set_freq(b->mode, b->timer, freq_hz);
    ledc_set_duty(b->mode, b->channel, b->duty);
    ledc_update_duty(b->mode, b->channel);
}

void offBuzzer(Buzzer *b) {
    ledc_set_duty(b->mode, b->channel, 0);
    ledc_update_duty(b->mode, b->channel);
}

void playMelody(Buzzer *b, const Melody *m, uint32_t nowMs) {
    b->index = 0;
    b->melody = m;
    startNote(b, nowMs);
    b->index++;
}

void updateBuzzer(Buzzer *b, uint32_t nowMs) {
    if (b->melody == NULL || (int32_t)(nowMs - b->nextChangeMs) < 0) {
        return;
    }

    if (!b->inGap) {
        b->inGap = true;
        b->nextChangeMs += b->gapMs;
        offBuzzer(b);
        return;
    }

    if (b->index >= b->melody->len) {
        if (b->melody->loop) {
            b->index = 0;
        } else {
            b->melody = NULL;
            return;
        }
    }

    startNote(b, nowMs);
    b->index++;
}

void stopBuzzer(Buzzer *b) {
    b->melody = NULL;
    offBuzzer(b);
}