idf_component_register(SRCS "app_synth_track.c" "app_synth_voice.c" "app_synth_op.c" "app_synth_env.c" "app_synth_osc.c" "app_synth.c"
                        INCLUDE_DIRS "include"
                        REQUIRES "app_midi_kbd" "app_event_bus" "sys_audio" "app_core")
