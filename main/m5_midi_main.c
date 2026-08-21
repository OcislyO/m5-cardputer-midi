#include "app_core.h"
#include "sys_bat.h"
#include "esp_log.h"

void app_main(void)
{
    uint8_t bat_percent;
    app_start();
    
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        sys_bat_get_percent(&bat_percent);
        ESP_LOGI("bat", "%d", bat_percent);
    }
}
