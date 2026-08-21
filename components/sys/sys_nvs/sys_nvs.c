#include "sys_nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "sys_nvs";

esp_err_t sys_nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "nvs init done");
    }
    return ret;
}
