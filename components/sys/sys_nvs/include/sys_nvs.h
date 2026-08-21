#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize NVS flash storage (erases and retries once if the
 *        partition is stale/new-format, matching common ESP-IDF practice).
 */
esp_err_t sys_nvs_init(void);

#ifdef __cplusplus
}
#endif
