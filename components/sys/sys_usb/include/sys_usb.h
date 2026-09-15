#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
} sys_usb_midi_msg_t;

esp_err_t sys_usb_init(void);

#ifdef __cplusplus
}
#endif
