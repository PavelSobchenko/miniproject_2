#include "pot.h"

void initPot(Pot *p) {
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = p->unit,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &p->handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = p->atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(p->handle, p->channel, &chan_cfg));
}

int32_t readPot(Pot *p) {
    int raw = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(p->handle, p->channel, &raw));

    if (raw < POT_IN_MIN) raw = POT_IN_MIN;
    if (raw > POT_IN_MAX) raw = POT_IN_MAX;

    int32_t out = (raw - POT_IN_MIN) * (p->outMaxVal - p->outMinVal) / (POT_IN_MAX - POT_IN_MIN) + p->outMinVal;
    return out;
}