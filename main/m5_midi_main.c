#include "app_core.h"
#include "sys_bat.h"
#include "esp_log.h"

void app_main(void)
{
    app_start();
    
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
