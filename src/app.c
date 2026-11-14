#include "app.h"
#include "config.h"
#include "logger.h"
#include "storage.h"
#include "led.h"
#include "button.h"
#include "pot.h"
#include "buzzer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const Note snd_startup[] = {
    {NOTE_C5, 16}, {NOTE_E5, 16}, {NOTE_G5, 8},
};
static const Note snd_error[] = {
    {NOTE_A4, 8}, {REST, 16}, {NOTE_A4, 8}, {REST, 16}, {NOTE_F4, 2},
};
static const Note snd_click[] = { {NOTE_C5, 32} };
static const Note snd_alarm_beeps[] = {
    {3000, 16}, {REST, 32}, {3000, 16}, {REST, 32},
    {3000, 16}, {REST, 8},
    {2400, 8},  {REST, 8},
};

const Melody MEL_STARTUP = MELODY(snd_startup, 140);
const Melody MEL_ERROR   = MELODY(snd_error,   120);
const Melody MEL_CLICK   = MELODY(snd_click,   120);
const Melody MEL_SHORT_CLICK   = MELODY(snd_click,   240);
const Melody MEL_ALARM_BEEPS = MELODY(snd_alarm_beeps, 120, .loop = true);

typedef enum {
    APP_MODE_DISARM,
    APP_MODE_ARMING,
    APP_MODE_ARMED,
    APP_MODE_ALARM,
} AppMode;

typedef struct {
    bool isEnabled;
    bool isPanic;
    uint8_t id;
    uint8_t detectionCount;
    uint32_t firstDetectionMs;
    Led *led;
} Zone;

typedef struct {
    Button buttons[BTN_COUNT];
    Led leds[LED_COUNT];
    Zone zones[ZONE_COUNT];
    Pot pot;
    Buzzer buzzer;
    AppMode mode;
    Logger logger;
    Storage zonesStorage;
    uint8_t zonesMask;
    uint32_t nextArmBeepMs;
} AppState;

static void onPanicPress(Button *b);
static void onArmPress(Button *b);
static void onZonePress(Button *b);
static void onArmLongPress(Button *b);
static void onZoneLongPress(Button *b);

static esp_timer_handle_t armingTimer;
static esp_timer_handle_t blinkTimer;
static esp_timer_handle_t alarmBlinkTimer;
static esp_timer_handle_t loggerTimer;

static AppState app = {
    .buttons = {
        [BTN_PANIC] = { .pin = PANIC_BTN_PIN, .useInterrupt = true, .onPress = &onPanicPress },
        [BTN_ARM] = { .pin = ARM_BTN_PIN, .onPress = &onArmPress, .onLongPress = &onArmLongPress },
        [ZONE_1_BTN] = { .pin = ZONE_1_BTN_PIN, .onPress = &onZonePress, .onLongPress = &onZoneLongPress, .ctx = &app.zones[ZONE_1] },
        [ZONE_2_BTN] = { .pin = ZONE_2_BTN_PIN, .onPress = &onZonePress, .onLongPress = &onZoneLongPress, .ctx = &app.zones[ZONE_2] },
    },
    .leds = {
        [RED_LED] = { .pin = RED_LED_PIN },
        [ZONE_1_LED] = { .pin = ZONE_1_LED_PIN },
        [ZONE_2_LED] = { .pin = ZONE_2_LED_PIN },
    },
    .zones = {
        [ZONE_1] = { .id = ZONE_1, .isEnabled = true, .led = &app.leds[ZONE_1_LED] },
        [ZONE_2] = { .id = ZONE_2, .isEnabled = true, .led = &app.leds[ZONE_2_LED] },
    },
    .pot = {
        .unit = ADC_UNIT_1,
        .channel = ADC_CHANNEL_0,
        .atten = ADC_ATTEN_DB_12,
        .outMinVal = ARM_PENDING_MIN_TIME_MS,
        .outMaxVal = ARM_PENDING_MAX_TIME_MS
    },
    .buzzer = {
        .pin = BUZZ_PIN,
        .timer = LEDC_TIMER_0,
        .channel = LEDC_CHANNEL_0,
        .res = LEDC_TIMER_10_BIT,
        .duty = (1 << (LEDC_TIMER_10_BIT - 1)),
        .mode = LEDC_LOW_SPEED_MODE
    },
    .logger = { .tag = "app" },
    .zonesStorage = { .ns = "alarm", .key = "zones", .data = &app.zonesMask, .size = sizeof(app.zonesMask) },
};

// ---------- "common" ------------------
static uint32_t getNowMs() {
    return esp_timer_get_time() / 1000;
}

static void loadZones() {
    if (storageLoad(&app.zonesStorage) != ESP_OK) return;
    for (size_t i = 0; i < ZONE_COUNT; i++) {
        app.zones[i].isEnabled = (app.zonesMask >> i) & 1;
    }
}
static void saveZones() {
    app.zonesMask = 0;
    for (size_t i = 0; i < ZONE_COUNT; i++) {
        if (app.zones[i].isEnabled) app.zonesMask |= (1 << i);
    }
    esp_err_t err = storageSave(&app.zonesStorage);
    if (err != ESP_OK) logError(&app.logger, "Failed to save zones: %s", esp_err_to_name(err));
}

static void playClick() {
    playMelody(&app.buzzer, &MEL_CLICK, getNowMs());
}
static void playShortClick() {
    playMelody(&app.buzzer, &MEL_SHORT_CLICK, getNowMs());
}

static const char *modeToStr(AppMode mode) {
    switch (mode) {
        case APP_MODE_DISARM: return "DISARMED";
        case APP_MODE_ARMING: return "ARMING";
        case APP_MODE_ARMED:  return "ARMED";
        case APP_MODE_ALARM:  return "ALARM";
        default:              return "UNKNOWN";
    }
}

static void disarm(bool useBuzzer) {
    esp_timer_stop(armingTimer);
    esp_timer_stop(blinkTimer);
    esp_timer_stop(alarmBlinkTimer);

    app.mode = APP_MODE_DISARM;

    setLed(&app.leds[RED_LED], false);
    for (size_t i = 0; i < ZONE_COUNT; i++) {
        app.zones[i].isPanic = false;
        if (app.zones[i].isEnabled) {
            setLed(app.zones[i].led, true);
        }
    }
    
    stopBuzzer(&app.buzzer);
    if (useBuzzer) {
        playClick();
    }

    logWarning(&app.logger, "disarmed");
}
static void alarm() {
    esp_timer_stop(armingTimer);
    esp_timer_stop(blinkTimer);
    esp_timer_start_periodic(alarmBlinkTimer, (uint64_t)ALARM_BLINK_PERIOD_MS * 1000);

    app.mode = APP_MODE_ALARM;
    setLed(&app.leds[RED_LED], false);
    logError(&app.logger, "ALARM!");
    playMelody(&app.buzzer, &MEL_ALARM_BEEPS, getNowMs());
}
static void handleArming() {
    uint32_t nowMs = getNowMs();
    if (app.mode != APP_MODE_ARMING || (int32_t)(nowMs - app.nextArmBeepMs) < 0) {
        return;
    }

    uint64_t expiryUs;
    esp_timer_get_expiry_time(armingTimer, &expiryUs);
    int32_t remainingMs = (int32_t)(expiryUs / 1000 - nowMs);
    uint16_t interval = remainingMs <= PANIC_BEEP_DURATION_MS ? PANIC_BEEP_INTERVAL_MS : CALM_BEEP_INTERVAL_MS;
    playShortClick();
    app.nextArmBeepMs = nowMs + interval;
}
// --------- end "common" ---------------

// ----------- "timers"  ----------------
static void onLoggerTimer(void *arg) {
    uint32_t nowMs = getNowMs();

    logWarning(&app.logger, "SYSTEM LOG: mode - %s; arming time: %lu", modeToStr(app.mode), readPot(&app.pot));
    for (size_t i = 0; i < ZONE_COUNT; i++) {
        Zone *z = &app.zones[i];
        uint32_t elapsedMs = nowMs - z->firstDetectionMs;
        unsigned long leftSec = 0;
        if (z->detectionCount > 0 && elapsedMs < FALSE_PANIC_DETECTION_TIME) {
            leftSec = (FALSE_PANIC_DETECTION_TIME - elapsedMs) / 1000;
        }
        const char *status = z->isEnabled ? "enabled" : "disabled";
        const char *panic = z->isPanic ? "true" : "false";

        logWarning(&app.logger, "SYSTEM LOG [ZONE-%d]: status - %s; panic - %s, detectionCount: %d, timeUntilDetectionReset: %lu", z->id + 1, status, panic, z->detectionCount, leftSec);
    }
}

static void onBlinkTimer(void *arg) {
    toggleLed(&app.leds[RED_LED]);
}

static void onAlarmBlinkTimer(void *arg) {
    toggleLed(&app.leds[RED_LED]);
    for (size_t i = 0; i < ZONE_COUNT; i++) {
        if (app.zones[i].isPanic) toggleLed(app.zones[i].led);
    }
}

static void onArmingTimer(void *arg) {
    logWarning(&app.logger, "armed");
    app.mode = APP_MODE_ARMED;
    esp_timer_start_periodic(blinkTimer, (uint64_t)BLINK_PERIOD_MS * 1000);
}
// ---------- end "timers" --------------

// ----------- "actions" ----------------
static void onPanicPress(Button *b) {
    if (app.mode == APP_MODE_ALARM) {
        return;
    }
    logError(&app.logger, "PANIC BUTTON PRESS!");
    alarm();
}

static void onArmPress(Button *b) {
    switch(app.mode) {
        case APP_MODE_DISARM: {
            uint32_t nowMs = getNowMs();
            logWarning(&app.logger, "arming");
            app.mode = APP_MODE_ARMING;
            int32_t armTimeMs = readPot(&app.pot);
            logInfo(&app.logger, "arming time: %lu", armTimeMs);
            playClick();
            app.nextArmBeepMs = nowMs + CALM_BEEP_INTERVAL_MS;
            esp_timer_start_once(armingTimer, (uint64_t)armTimeMs * 1000);
            break;
        }
        case APP_MODE_ARMING:
        case APP_MODE_ARMED:
        case APP_MODE_ALARM:
            disarm(app.mode != APP_MODE_ALARM);
            break;
    }
}

static void onArmLongPress(Button *b) {
    disarm(false);
}

static void onZonePress(Button *b) {
    uint32_t nowMs = getNowMs();
    Zone *zone = (Zone *)b->ctx;

    if(!zone->isEnabled) {
        logWarning(&app.logger, "Detection in zone: %d; Zone is disabled: ignoring", zone->id + 1);
        return;
    }

    if (zone->isPanic) {
        logWarning(&app.logger, "Detection in zone: %d; Zone is already in alarm", zone->id + 1);
        return;
    }

    if (zone->detectionCount == 0 ||
        (nowMs - zone->firstDetectionMs) > FALSE_PANIC_DETECTION_TIME) {
        zone->detectionCount = 0;
        zone->firstDetectionMs = nowMs;
    }

    zone->detectionCount++;

    if (zone->detectionCount >= FALSE_PANIC_DETECTION_COUNT) {
        zone->isEnabled = false;
        zone->isPanic = false;
        saveZones();
        setLed(zone->led, false);
        logError(&app.logger, "Zone %d is disabled due to %d triggers in %d sec",
                 zone->id + 1, FALSE_PANIC_DETECTION_COUNT, FALSE_PANIC_DETECTION_TIME / 1000);
        return;
    }

    if (app.mode == APP_MODE_ARMED || app.mode == APP_MODE_ALARM) {
        zone->isPanic = true;
        logError(&app.logger, "Detection in Zone-%d!!", zone->id + 1);
        if (app.mode == APP_MODE_ARMED) {
            alarm();
        }
        return;
    }

    uint32_t elapsedMs = nowMs - zone->firstDetectionMs;
    unsigned long leftSec   = (FALSE_PANIC_DETECTION_TIME - elapsedMs) / 1000;

    logWarning(&app.logger,
        "Detection in zone: %d; triggers count: %d; time for reset count: %lu sec",
        zone->id + 1,
        zone->detectionCount,
        leftSec);
}

static void onZoneLongPress(Button *b) {
    Zone *zone = (Zone *)b->ctx;
    zone->isEnabled = !zone->isEnabled;
    saveZones();
    setLed(zone->led, zone->isEnabled);
    zone->isPanic = false;
    logWarning(&app.logger, "Zone %d is %s", zone->id + 1, zone->isEnabled ? "enabled" : "disabled");

    if (zone->isEnabled) {
        zone->detectionCount = 0;
    }

    if (app.mode == APP_MODE_ALARM) {
        return;
    }

    if (zone->isEnabled) {
        playClick();
    } else {
        playShortClick();
    }
}
// ---------- end "actions" -------------

void appInit() {
    ESP_ERROR_CHECK(storageInit(&app.zonesStorage));
    loadZones();

    for (size_t i = 0; i < LED_COUNT; i++) initLed(&app.leds[i]);
    for (size_t i = 0; i < BTN_COUNT; i++) initButton(&app.buttons[i]);
    for (size_t i = 0; i < ZONE_COUNT; i++) setLed(app.zones[i].led, app.zones[i].isEnabled);
    initPot(&app.pot);
    initBuzzer(&app.buzzer);

    const esp_timer_create_args_t armingArgs = {
        .callback = &onArmingTimer,
        .name = "arming",
    };
    ESP_ERROR_CHECK(esp_timer_create(&armingArgs, &armingTimer));

    const esp_timer_create_args_t blinkArgs = {
        .callback = &onBlinkTimer,
        .name = "blink",
    };
    ESP_ERROR_CHECK(esp_timer_create(&blinkArgs, &blinkTimer));

    const esp_timer_create_args_t alarmBlinkArgs = {
        .callback = &onAlarmBlinkTimer,
        .name = "alarmBlink",
    };
    ESP_ERROR_CHECK(esp_timer_create(&alarmBlinkArgs, &alarmBlinkTimer));

    const esp_timer_create_args_t loggerTimerArgs = {
        .callback = &onLoggerTimer,
        .name = "logger",
    };
    ESP_ERROR_CHECK(esp_timer_create(&loggerTimerArgs, &loggerTimer));
}

void appStart() {
    playMelody(&app.buzzer, &MEL_STARTUP, getNowMs());
    logInfo(&app.logger, "System is ready!");
    esp_timer_start_periodic(loggerTimer, (uint64_t)LOGGER_INTERVAL_MS * 1000);

    while(1) {
        uint32_t nowMs = getNowMs();

        handleArming();
        updateBuzzer(&app.buzzer, nowMs);

        for (size_t i = 0; i < BTN_COUNT; i++) {
            Button *b = &app.buttons[i];
            if (b->state != BTN_STATE_IDLE || b->pending || !b->useInterrupt) {
                b->pending = false;
                b->state = handleButtonUpdate(b, nowMs);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

