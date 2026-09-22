idf_component_register(SRCS "asset.c" "app_ui.c"
                        INCLUDE_DIRS "include"
                        REQUIRES sys_dsp sys_kbd sys_bat app_core)
