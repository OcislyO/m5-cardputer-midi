#include "sys_usb.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"


#define TUSB_DESCRIPTOR_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN)

static const uint8_t s_usb_config_desc[] = {

    TUD_CONFIG_DESCRIPTOR(
        1,      // configuration number
        2,      // interface count
        0,      // string index
        TUSB_DESCRIPTOR_TOTAL_LEN,
        0,
        100
    ),

    TUD_MIDI_DESCRIPTOR(
        0,      // interface number
        4,      // string index
        0x01,   // OUT endpoint
        0x81,   // IN endpoint
        64      // packet size
    ),
};

static const char *s_str_desc[] = {
    (const char[]){0x09, 0x04}, // English
    "Alrescha",
    "Cardputer MIDI",
    "MIDI",
};

esp_err_t sys_usb_init() {
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();

    tusb_cfg.descriptor.device = NULL;
    tusb_cfg.descriptor.string = s_str_desc;
    tusb_cfg.descriptor.string_count = sizeof(s_str_desc) / sizeof(s_str_desc[0]);
    tusb_cfg.descriptor.full_speed_config = s_usb_config_desc;
    return (tinyusb_driver_install(&tusb_cfg));
}

bool sys_usb_midi_available(void)
{
    return tud_midi_available();
}

size_t sys_usb_midi_read(uint8_t *data, size_t size)
{
    return tud_midi_stream_read(data, size);
}

size_t sys_usb_midi_write(const uint8_t *data, size_t size)
{
    return tud_midi_stream_write(0, data, size);
}