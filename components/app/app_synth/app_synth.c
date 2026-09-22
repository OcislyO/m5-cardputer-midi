#include "app_synth.h"
#include "app_synth_track.h"
#include "app_synth_voice.h"
#include "app_event_bus.h"
#include "app_midi_kbd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "esp_log.h"

#define APP_SYNTH_ENGINE_TASK_STACK 3072
#define APP_SYNTH_ENGINE_TASK_PRIO  4
#define APP_SYNTH_TASK_EXIT_TIMEOUT_MS 500

// I2S 发送用的是环形 DMA（I2S_CHANNEL_DEFAULT_CONFIG，6 个描述符）。引擎任务
// 一停就没人再喂数据，DMA 会把缓冲里剩下的最后几帧反复播出去 —— 如果那正好
// 是某个正在响的音，就会变成一直不断的音。所以退出时按 DMA 深度刷静音。
#define APP_SYNTH_DMA_FLUSH_FRAMES 6

static const char *TAG = "app_synth";

app_synth_app_t app_synth_app;

static volatile bool s_run; // false = 请求引擎任务退出

static int16_t s_wavetables[APP_SYNTH_WAVE_COUNT][APP_SYNTH_WT_SIZE];

app_synth_voice_t *note_map[MAX_TRACK_COUNT][128];  // 记录note->voice

sys_audio_frame_t app_synth_frame;

QueueHandle_t app_synth_midi_queue;

static esp_err_t app_synth_init(app_t *app);
static esp_err_t app_synth_uninit(app_t *app);
static size_t app_synth_get_state(struct app_s *app, int16_t state, void *out, uint8_t size);
static size_t app_synth_get_data(struct app_s *app, int16_t state, void *out, uint8_t size, ...);
static esp_err_t app_synth_command(app_t *app, int16_t command, ...);

static void app_synth_wavetable_generate(void);

TaskHandle_t app_synth_engine_task_handle;
static void app_synth_engine_task(void *arg);

app_t *app_synth_app_init() {
    app_synth_app.base.state = APP_STATE_UNINIT;
    app_synth_app.base.id = APP_ID_SYNTH;
    app_synth_app.base.ctx = &app_synth_app;
    app_synth_app.base.init = app_synth_init;
    app_synth_app.base.uninit = app_synth_uninit;
    app_synth_app.base.get_state = app_synth_get_state;
    app_synth_app.base.get_data = app_synth_get_data;
    app_synth_app.base.command = app_synth_command;
    return &app_synth_app.base;
}

static esp_err_t app_synth_init(app_t *app) {
    esp_err_t err = ESP_OK;
    if (app->state != APP_STATE_UNINIT)
        goto ret;

    err = sys_audio_init();
    if (err)
        goto ret;

    app_synth_wavetable_generate();

    memset(note_map, 0, sizeof(note_map));

    app_synth_track_init();

    app_synth_midi_queue = xQueueCreate(10, sizeof(app_event_midi_t));
    if (! app_synth_midi_queue) {
        err =  ESP_ERR_NO_MEM;
        goto ret;
    }

    // 订阅放在 init 而不是任务里：任务只管跑引擎，退出时也不用再动总线
    err = app_event_bus_subscribe(EVENT_MIDI_NOTE, app_synth_midi_queue);
    if (err) {
        vQueueDelete(app_synth_midi_queue);
        app_synth_midi_queue = NULL;
        goto ret;
    }

    s_run = true;
    if (xTaskCreate(app_synth_engine_task, "app_synth_engine", APP_SYNTH_ENGINE_TASK_STACK, NULL, APP_SYNTH_ENGINE_TASK_PRIO, &app_synth_engine_task_handle) != pdPASS) {
        app_event_bus_unsubscribe(EVENT_MIDI_NOTE, app_synth_midi_queue);
        vQueueDelete(app_synth_midi_queue);
        app_synth_midi_queue = NULL;
        err =  ESP_ERR_NO_MEM;
        goto ret;
    }

    app->state = APP_STATE_RUNNING;

    ret:
        return err;
}

static esp_err_t app_synth_uninit(app_t *app) {
    esp_err_t err = ESP_OK;
    if (app->state != APP_STATE_RUNNING)
    {
        err = ESP_ERR_INVALID_STATE;
        goto ret;
    }

    s_run = false; // 引擎循环每帧检查一次，最多等一帧（约 11ms）就退出
    if (!app_task_wait_stopped(&app_synth_engine_task_handle, APP_SYNTH_TASK_EXIT_TIMEOUT_MS)) {
        ESP_LOGE(TAG, "引擎任务未退出，保留队列");
        err = ESP_ERR_TIMEOUT;
        goto ret;
    }

    // 引擎已经停了，现在只有本任务在写 I2S，把 DMA 环形缓冲整圈覆写成静音
    static const int16_t silence[SYS_AUDIO_FRAME_SAMPLES];
    for (int i = 0; i < APP_SYNTH_DMA_FLUSH_FRAMES; i++) {
        sys_audio_mix(silence);
        sys_audio_send_frame();
    }

    // 声部池是全局数组（零值正好是 VOICE_STATE_FREE），init 并不会清它，
    // 所以这里把还占着的声部放回空闲，否则再次 init 会带着旧状态复活。
    memset(app_synth_voice_pool, 0, sizeof(app_synth_voice_pool));
    memset(note_map, 0, sizeof(note_map));

    // 顺序重要：先退订，再删队列，否则总线可能往已释放的队列发事件
    app_event_bus_unsubscribe(EVENT_MIDI_NOTE, app_synth_midi_queue);
    vQueueDelete(app_synth_midi_queue);
    app_synth_midi_queue = NULL;

    app->state = APP_STATE_UNINIT;

    ret:
        return err;
}

static size_t app_synth_get_state(struct app_s *app, int16_t state, void *out, uint8_t size) {
    size_t bytes = 0;

    if (app->id != app_synth_app.base.id)
        return bytes;

    if (state >= APP_SYNTH_STATE_MAX || state < 0)
        return bytes;

    switch (state)
    {
    case APP_SYNTH_STATE_MASTER_LEVEL:
        bytes = sizeof(app_synth_app.state.master_level);
        if (size < bytes)
            bytes = size;
        memcpy(out, &app_synth_app.state.master_level, bytes);
        break;
    
    default:
        break;
    }
    return bytes;
}

// 把 track_list[track] 里某个算子参数列表从 op_id 起到末尾拷给 out，最多
// size 字节：传单个元素大小 = 只读一个算子；op_id 传 0 且 size 传整段大小 =
// 一次读出整列。注意元素大小按字段的真实类型算（如 op_wave 是枚举，4 字节）。
static size_t app_synth_get_data_field(const void *list, size_t elem_size, int op_id, void *out, uint8_t size)
{
    if (op_id < 0 || (size_t)op_id >= MAX_OPERATOR_COUNT)
        return 0;

    size_t avail = (MAX_OPERATOR_COUNT - (size_t)op_id) * elem_size;
    if (size < avail)
        avail = size;

    memcpy(out, (const uint8_t *)list + (size_t)op_id * elem_size, avail);
    return avail;
}

static size_t app_synth_get_data(struct app_s *app, int16_t state, void *out, uint8_t size, ...) {
    size_t bytes = 0;
    int track_id, op_id;

    if (app->id != app_synth_app.base.id)
        return bytes;

    if (state >= APP_SYNTH_DATA_MAX || state < 0)
        return bytes;

    va_list args;
    va_start(args, size);

    switch (state)
    {
    case APP_SYNTH_DATA_TRACK:
        track_id = va_arg(args, int);
        if (track_id < 0 || track_id >= MAX_TRACK_COUNT)
            break;

        // 整条轨道一次拷走，调用方按 app_synth_track_t 解释
        bytes = sizeof(track_list[0]);
        if (size < bytes)
            bytes = size;

        memcpy(out, &track_list[track_id], bytes);
        break;

    case APP_SYNTH_DATA_TRACK_LEVEL:
        track_id = va_arg(args, int);
        if (track_id < 0 || track_id >= MAX_TRACK_COUNT)
            break;

        bytes = sizeof(track_list[0].voice_level);
        if (size < bytes)
            bytes = size;

        memcpy(out, &track_list[track_id].voice_level, bytes);
        break;
        
    case APP_SYNTH_DATA_OP_WAVE:
        track_id = va_arg(args, int);
        op_id = va_arg(args, int);
        if (track_id < 0 || track_id >= MAX_TRACK_COUNT)
            break;

        bytes = app_synth_get_data_field(track_list[track_id].op_wave,
                                         sizeof(track_list[track_id].op_wave[0]), op_id, out, size);
        break;

    case APP_SYNTH_DATA_OP_LEVEL:
        track_id = va_arg(args, int);
        op_id = va_arg(args, int);
        if (track_id < 0 || track_id >= MAX_TRACK_COUNT)
            break;

        bytes = app_synth_get_data_field(track_list[track_id].op_level,
                                         sizeof(track_list[track_id].op_level[0]), op_id, out, size);
        break;

    case APP_SYNTH_DATA_OP_COARSE:
        track_id = va_arg(args, int);
        op_id = va_arg(args, int);
        if (track_id < 0 || track_id >= MAX_TRACK_COUNT)
            break;

        bytes = app_synth_get_data_field(track_list[track_id].op_coarse,
                                         sizeof(track_list[track_id].op_coarse[0]), op_id, out, size);
        break;

    case APP_SYNTH_DATA_OP_ENV:
        track_id = va_arg(args, int);
        op_id = va_arg(args, int);
        if (track_id < 0 || track_id >= MAX_TRACK_COUNT)
            break;

        bytes = app_synth_get_data_field(track_list[track_id].op_env,
                                         sizeof(track_list[track_id].op_env[0]), op_id, out, size);
        break;

    case APP_SYNTH_DATA_ALGORITHM:
        track_id = va_arg(args, int);
        op_id = va_arg(args, int);
        if (track_id < 0 || track_id >= MAX_TRACK_COUNT)
            break;

        bytes = app_synth_get_data_field(track_list[track_id].fm_metrix,
                                         sizeof(track_list[track_id].fm_metrix[0]), op_id, out, size);
        break;
    
    default:
        break;
    }

    va_end(args);

    return bytes;
}

static esp_err_t app_synth_command(app_t *app, int16_t command, ...) {
    esp_err_t err = ESP_OK;
    int track, op;
    va_list args;
    va_start(args, command);

    const app_synth_cmd_id_t cmd = (app_synth_cmd_id_t)command;
    
    switch (cmd)
    {
    case APP_SYNTH_CMD_SET_MASTER_LEVEL:
        uint8_t m_level = (uint8_t)va_arg(args, int);
        if (m_level > 100)
            m_level = 100;
        
        app_synth_app.state.master_level = m_level;
        sys_audio_set_volume(app_synth_app.state.master_level);
        break;

    case APP_SYNTH_CMD_SET_TRACK_LEVEL:
        track = va_arg(args, int);
        float level = (float)va_arg(args, double);
        app_synth_track_set_level(track, level);
        break;
    
    case APP_SYNTH_CMD_SET_OP_WAVE:
        track = va_arg(args, int);
        op = va_arg(args, int);
        uint8_t wave = (uint8_t)va_arg(args, int);
        app_synth_track_set_op_wave(track, op, wave);
        break;
    
    case APP_SYNTH_CMD_SET_OP_LEVEL:
        track = va_arg(args, int);
        op = va_arg(args, int);
        float op_level = (float)va_arg(args, double);
        app_synth_track_set_op_level(track, op, op_level);
        break;
    
    case APP_SYNTH_CMD_SET_OP_COARSE:
        track = va_arg(args, int);
        op = va_arg(args, int);
        uint8_t coarse = (uint8_t)va_arg(args, int);
        app_synth_track_set_op_coarse(track, op, coarse);
        break;
    
    case APP_SYNTH_CMD_SET_OP_ENV:
        track = va_arg(args, int);
        op = va_arg(args, int);
        float A = (float)va_arg(args, double);
        float D = (float)va_arg(args, double);
        float S = (float)va_arg(args, double);
        float R = (float)va_arg(args, double);
        app_synth_track_set_op_env(track, op, A, D, S, R);
        break;
    
    case APP_SYNTH_CMD_SET_ALGORITHM:
        track = va_arg(args, int);
        uint8_t carrier = (uint8_t)va_arg(args, int);
        uint8_t modulater = (uint8_t)va_arg(args, int);
        bool flag = (bool)va_arg(args, int);
        app_synth_track_set_op_algorithm(track, carrier, modulater, flag);
        break;
    
    default:
        break;
    }

    va_end(args);

    return err;
}

static void app_synth_wavetable_generate(void) {
    for (size_t i = 0; i < APP_SYNTH_WT_SIZE; i++) {
        float phase = (float)i / APP_SYNTH_WT_SIZE; // 0..1 through one cycle

        s_wavetables[APP_SYNTH_WAVE_SINE][i] = (int16_t)(sinf(2.0f * (float)M_PI * phase) * INT16_MAX);

        // Symmetric triangle: rises 0..1..0..-1..0 across the cycle.
        float tri = phase < 0.5f ? (4.0f * phase - 1.0f) : (3.0f - 4.0f * phase);
        s_wavetables[APP_SYNTH_WAVE_TRIANGLE][i] = (int16_t)(tri * INT16_MAX);

        // Falling sawtooth: +1 at phase 0, ramps linearly down to -1.
        s_wavetables[APP_SYNTH_WAVE_SAW][i] = (int16_t)((1.0f - 2.0f * phase) * INT16_MAX);

        s_wavetables[APP_SYNTH_WAVE_SQUARE][i] = (phase < 0.5f) ? INT16_MAX : -INT16_MAX;
    }
}

int16_t app_synth_wavetable_sample(app_synth_wave_t wave, uint32_t phase)
{
    uint8_t index = (uint8_t)(phase >> APP_SYNTH_WT_FRAC_BITS);
    uint8_t frac = (uint8_t)(phase >> 16);  // 只取bit17~24补偿精度减少计算量

    const int16_t *table = s_wavetables[wave];
    int16_t a = table[index];
    int16_t b = table[(index + 1) & (APP_SYNTH_WT_SIZE - 1)];

    return a + (((b - a) * frac) >> 8);
}

static void app_synth_event_receive(void) {
    app_event_midi_t event;
    app_synth_voice_t *voice;
    float freq;

    while (xQueueReceive(app_synth_midi_queue, &event, 0))  // 收到数据继续接收，没收到返回pdfalse退出循环
    {
        
        if (event.channel >= MAX_TRACK_COUNT)
        {
            continue;
        }
        
        if (event.type == APP_MIDI_EVENT_NOTE_ON) {
            freq = 440.0f * powf(2.0f, (event.note - 69) / 12.0f);
            voice = app_synth_voice_on(event.channel, freq);
            if (! voice)
                continue;
            
            note_map[event.channel][event.note] = voice;

        } else if (event.type == APP_MIDI_EVENT_NOTE_OFF) {
            voice = note_map[event.channel][event.note];
            if (! voice)
                continue;

            app_synth_voice_off(voice);
            note_map[event.channel][event.note] = 0;
        }
    }
}

static void app_synth_engine_task(void *arg) {
    app_synth_voice_t *voice;
    int32_t sample[MAX_TRACK_COUNT];
    uint32_t tick = 0;

    while (s_run)
    {
        app_synth_event_receive();
        tick = xTaskGetTickCount();
        for (size_t frame_index = 0; frame_index < SYS_AUDIO_FRAME_SAMPLES; frame_index++)
        {
            for (size_t voice_index = 0; voice_index < MAX_VOICE_COUNT; voice_index++)
            {
                voice = &app_synth_voice_pool[voice_index];

                if (voice->state == VOICE_STATE_FREE)
                    continue;

                sample[voice->from_track->midi_channel] += app_synth_voice_sample(voice);
            }

            int32_t temp = 0;
            for (size_t track_index = 0; track_index < MAX_TRACK_COUNT; track_index++)
            {
                temp += sample[track_index] * track_list[track_index].voice_level;
                sample[track_index] = 0;
            }

            if (temp > 0x7fff)
                temp = 0x7fff;

            if (temp < -32768)
                temp = -32768;

            app_synth_frame.samples[frame_index] = (int16_t)temp;
        }

        sys_audio_mix(app_synth_frame.samples);
        memset(app_synth_frame.samples, 0, 512);
        // ESP_LOGI("eg", "%ld", xTaskGetTickCount() - tick);

        sys_audio_send_frame();
    }

    app_synth_engine_task_handle = NULL;
    vTaskDelete(NULL);
}