#pragma once
#include <stdint.h>
#include <stddef.h>
#include "driver/ledc.h"

#define REST 0
#define NOTE_C4 262
#define NOTE_D4 294
#define NOTE_E4 330
#define NOTE_F4 349
#define NOTE_G4 392
#define NOTE_A4 440
#define NOTE_B4 494
#define NOTE_C5 523
#define NOTE_D5 587
#define NOTE_E5 659
#define NOTE_G5 784

typedef struct Buzzer Buzzer;
typedef struct Note Note;
typedef struct Melody Melody;

struct Note {
    uint16_t freq;
    int8_t   div;
};

struct Melody {
    bool loop;
    const Note *notes;
    size_t len;
    uint16_t bpm;
};

struct Buzzer {
    uint8_t pin;
    ledc_channel_t channel;
    ledc_timer_t timer;
    ledc_timer_bit_t res;
    ledc_mode_t mode;
    uint32_t duty;

    const Melody *melody;
    size_t index;
    bool inGap;
    uint32_t gapMs;
    uint32_t nextChangeMs;
};

#define MELODY(arr, tempo, ...) { .notes = (arr), .len = sizeof(arr)/sizeof((arr)[0]), .bpm = (tempo), __VA_ARGS__ }
extern const Melody MEL_STARTUP;
extern const Melody MEL_ERROR;
extern const Melody MEL_CLICK;

void initBuzzer(Buzzer *b);
void toneBuzzer(Buzzer *b, uint32_t freq_hz);
void offBuzzer(Buzzer *b);
void playMelody(Buzzer *b, const Melody *m, uint32_t nowMs);
void updateBuzzer(Buzzer *b, uint32_t nowMs);
void stopBuzzer(Buzzer *b);