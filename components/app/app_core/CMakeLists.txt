idf_component_register(SRCS "app_core.c"
                        INCLUDE_DIRS "include"
                        REQUIRES sys_nvs bsp_i2c bsp_spi bsp_gpio sys_dsp sys_bat sys_audio sys_usb sys_wlan app_ui app_midi_kbd app_event_bus app_synth app_seq app_web)
