idf_component_register(SRCS "sys_kbd.c"
                        INCLUDE_DIRS "include"
                        REQUIRES drv_tca8418 bsp_gpio)
