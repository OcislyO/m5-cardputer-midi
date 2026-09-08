idf_component_register(SRCS "app_midi_kbd.c"
                        INCLUDE_DIRS "include"
                        REQUIRES sys_kbd app_event_bus app_core)
