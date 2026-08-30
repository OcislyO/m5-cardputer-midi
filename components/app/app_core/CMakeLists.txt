idf_component_register(SRCS "app_core.c"
                        INCLUDE_DIRS "include"
                        REQUIRES sys_nvs bsp_i2c bsp_spi bsp_gpio sys_dsp sys_bat sys_audio app_ui app_midi app_synth)
