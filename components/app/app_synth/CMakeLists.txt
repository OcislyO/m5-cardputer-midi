idf_component_register(SRCS "app_synth.c" "app_synth_wavetable.c" "app_synth_op.c" "app_synth_voice.c" "app_synth_render.c" "app_synth_midi.c"
                        INCLUDE_DIRS "include"
                        REQUIRES bsp_i2s drv_es8311 app_midi)
