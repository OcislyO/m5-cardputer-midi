#include "sys_bat.h"
#include "bsp_adc.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// bsp_adc's VBAT channel sits after a 1/2 divider, so the real battery
// voltage is twice what it reports.
#define SYS_BAT_DIVIDER_RATIO    2
#define SYS_BAT_SAMPLE_PERIOD_US (5000 * 1000)
#define SYS_BAT_EMA_SHIFT        3 // alpha = 1/8, smooths out ADC/switching noise

static const char *TAG = "sys_bat";

// Rough single-cell Li-ion/LiPo open-circuit-voltage to state-of-charge curve,
// sorted high-to-low mV. "approximate" per the caller's own requirement.
typedef struct {
    uint16_t mv;
    uint8_t  percent;
} sys_bat_curve_point_t;

static const sys_bat_curve_point_t s_curve[] = {
    { 4200, 100 },
    { 4060, 90 },
    { 3980, 80 },
    { 3920, 70 },
    { 3870, 60 },
    { 3820, 50 },
    { 3790, 40 },
    { 3770, 30 },
    { 3730, 20 },
    { 3680, 10 },
    { 3300, 0 },
};
#define SYS_BAT_CURVE_LEN (sizeof(s_curve) / sizeof(s_curve[0]))

static esp_timer_handle_t s_timer;
static SemaphoreHandle_t s_lock;
static int s_ema_mv = -1;
static uint8_t s_percent;
static bool s_initialized = false;

static uint8_t voltage_to_percent(uint16_t mv)
{
    if (mv >= s_curve[0].mv) {
        return 100;
    }
    if (mv <= s_curve[SYS_BAT_CURVE_LEN - 1].mv) {
        return 0;
    }

    for (size_t i = 0; i < SYS_BAT_CURVE_LEN - 1; i++) {
        uint16_t hi_mv = s_curve[i].mv;
        uint16_t lo_mv = s_curve[i + 1].mv;
        if (mv <= hi_mv && mv >= lo_mv) {
            uint8_t hi_pct = s_curve[i].percent;
            uint8_t lo_pct = s_curve[i + 1].percent;
            return lo_pct + (uint32_t)(mv - lo_mv) * (hi_pct - lo_pct) / (hi_mv - lo_mv);
        }
    }
    return 0; // unreachable, mv is bounded by the checks above
}

static void sys_bat_sample(void *arg)
{
    int adc_mv;
    esp_err_t err = bsp_adc_read_voltage_mv(&adc_mv);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "adc read failed: %s", esp_err_to_name(err));
        return;
    } else {ESP_LOGI(TAG, "%d", adc_mv);}
    int batt_mv = adc_mv * SYS_BAT_DIVIDER_RATIO;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_ema_mv = (s_ema_mv < 0) ? batt_mv : s_ema_mv + ((batt_mv - s_ema_mv) >> SYS_BAT_EMA_SHIFT);
    s_percent = voltage_to_percent((uint16_t)s_ema_mv);
    xSemaphoreGive(s_lock);
}

esp_err_t sys_bat_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t err = bsp_adc_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bsp_adc_init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const esp_timer_create_args_t timer_args = {
        .callback = sys_bat_sample,
        .name = "sys_bat",
    };
    err = esp_timer_create(&timer_args, &s_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create failed: %s", esp_err_to_name(err));
        return err;
    }

    sys_bat_sample(NULL); // seed a reading so callers don't have to wait a full period

    err = esp_timer_start_periodic(s_timer, SYS_BAT_SAMPLE_PERIOD_US);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_start_periodic failed: %s", esp_err_to_name(err));
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "battery monitor init done (period=%dms)", SYS_BAT_SAMPLE_PERIOD_US / 1000);
    return ESP_OK;
}

esp_err_t sys_bat_get_percent(uint8_t *out_percent)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool have_sample = (s_ema_mv >= 0);
    if (have_sample) {
        *out_percent = s_percent;
    }
    xSemaphoreGive(s_lock);

    return have_sample ? ESP_OK : ESP_ERR_INVALID_STATE;
}
