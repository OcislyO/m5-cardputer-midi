idf_component_register(SRCS "drv_bmi270.c"
                        INCLUDE_DIRS "include"
                        REQUIRES bsp_i2c espressif__bmi270)
