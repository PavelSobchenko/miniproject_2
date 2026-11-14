#pragma once
#include <stddef.h>
#include "esp_err.h"
#include "nvs.h"

typedef struct Storage Storage;

struct Storage {
    const char *ns;
    const char *key;
    void *data;
    size_t size;
    nvs_handle_t handle;
};

esp_err_t storageInit(Storage *s);
esp_err_t storageLoad(Storage *s);   // ESP_ERR_NVS_NOT_FOUND при першому запуску
esp_err_t storageSave(Storage *s);
void storageDeinit(Storage *s);