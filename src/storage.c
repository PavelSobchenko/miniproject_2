#include "storage.h"
#include "nvs_flash.h"
#include <stdbool.h>

static bool nvsReady = false;

static esp_err_t nvsInitOnce(void) {
    if (nvsReady) return ESP_OK;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err == ESP_OK) err = nvs_flash_init();
    }
    if (err == ESP_OK) nvsReady = true;
    return err;
}

esp_err_t storageInit(Storage *s) {
    esp_err_t err = nvsInitOnce();
    if (err != ESP_OK) return err;
    return nvs_open(s->ns, NVS_READWRITE, &s->handle);
}

esp_err_t storageLoad(Storage *s) {
    size_t len = 0;
    esp_err_t err = nvs_get_blob(s->handle, s->key, NULL, &len);
    if (err != ESP_OK) return err;
    if (len != s->size) return ESP_ERR_INVALID_SIZE;
    return nvs_get_blob(s->handle, s->key, s->data, &len);
}

esp_err_t storageSave(Storage *s) {
    esp_err_t err = nvs_set_blob(s->handle, s->key, s->data, s->size);
    if (err == ESP_OK) err = nvs_commit(s->handle);
    return err;
}

void storageDeinit(Storage *s) {
    nvs_close(s->handle);
}