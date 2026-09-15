#include "sys_wlan.h"
#include "sys_nvs.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "nvs.h" // ESP_ERR_NVS_NOT_FOUND, for "nothing saved yet"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "sys_wlan";

#define SYS_WLAN_CONNECTED_BIT BIT0
#define SYS_WLAN_FAILED_BIT    BIT1

typedef struct {
    sys_wlan_event_cb_t cb;
    void *ctx;
} sys_wlan_cb_slot_t;

static bool s_initialized;
static esp_netif_t *s_netif_sta;
static esp_netif_t *s_netif_ap;
static SemaphoreHandle_t s_lock;
static EventGroupHandle_t s_bits;
static esp_timer_handle_t s_reconnect_timer;
static esp_timer_handle_t s_apstop_timer;

// Everything below is guarded by s_lock: the WiFi event handler runs on the
// system event task while the API is called from app/web tasks.
static sys_wlan_cb_slot_t s_cbs[CONFIG_SYS_WLAN_MAX_EVENT_CBS];
static sys_wlan_state_t s_state = SYS_WLAN_STATE_STOPPED;
static bool s_wifi_started;  // esp_wifi_start() has been called
static bool s_sta_enabled;   // station interface wanted
static bool s_ap_enabled;    // SoftAP wanted
static bool s_want_connect;  // keep (re)connecting the station
static bool s_scanning;
static uint32_t s_retries;
static char s_sta_ssid[SYS_WLAN_SSID_MAX_LEN + 1];
static char s_sta_ip[SYS_WLAN_IP_STR_LEN];
static char s_ap_ssid[SYS_WLAN_SSID_MAX_LEN + 1];

static void wlan_lock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void wlan_unlock(void)
{
    xSemaphoreGive(s_lock);
}

static void wlan_copy_str(char *dst, size_t dst_size, const char *src, size_t src_len)
{
    size_t len = (src_len < dst_size - 1) ? src_len : dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

// Callbacks are invoked outside the lock (they are free to call back into
// sys_wlan_get_status()), so work off a snapshot of the table.
static void wlan_notify(sys_wlan_event_t event)
{
    sys_wlan_cb_slot_t slots[CONFIG_SYS_WLAN_MAX_EVENT_CBS];

    wlan_lock();
    memcpy(slots, s_cbs, sizeof(slots));
    wlan_unlock();

    for (size_t i = 0; i < CONFIG_SYS_WLAN_MAX_EVENT_CBS; i++) {
        if (slots[i].cb != NULL) {
            slots[i].cb(event, slots[i].ctx);
        }
    }
}

// Point the driver at whichever interfaces are currently wanted. Called with
// the lock held by the API functions that flip s_sta_enabled/s_ap_enabled.
static esp_err_t wlan_sync_mode_locked(void)
{
    wifi_mode_t want = WIFI_MODE_NULL;
    if (s_sta_enabled && s_ap_enabled) {
        want = WIFI_MODE_APSTA;
    } else if (s_sta_enabled) {
        want = WIFI_MODE_STA;
    } else if (s_ap_enabled) {
        want = WIFI_MODE_AP;
    }

    wifi_mode_t current = WIFI_MODE_NULL;
    esp_wifi_get_mode(&current);
    if (current == want) {
        return ESP_OK;
    }
    return esp_wifi_set_mode(want);
}

// Start or stop the radio to match the wanted interfaces. Returns, via
// out_was_started, whether the driver was already running before the call, so
// callers know if a fresh WIFI_EVENT_STA_START is on its way.
static esp_err_t wlan_sync_power_locked(bool *out_was_started)
{
    bool want_running = s_sta_enabled || s_ap_enabled;
    if (out_was_started != NULL) {
        *out_was_started = s_wifi_started;
    }
    if (want_running == s_wifi_started) {
        return ESP_OK;
    }

    esp_err_t err = want_running ? esp_wifi_start() : esp_wifi_stop();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_%s failed: %s", want_running ? "start" : "stop", esp_err_to_name(err));
        return err;
    }
    s_wifi_started = want_running;
    return ESP_OK;
}

static void wlan_reconnect_timer_cb(void *arg)
{
    wlan_lock();
    bool want = s_want_connect;
    wlan_unlock();

    if (!want) {
        return;
    }
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "reconnect failed to start: %s", esp_err_to_name(err));
    }
}

static void wlan_arm_reconnect(void)
{
    esp_timer_stop(s_reconnect_timer); // no-op if it is not armed
    esp_err_t err = esp_timer_start_once(s_reconnect_timer, CONFIG_SYS_WLAN_RECONNECT_DELAY_MS * 1000ULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "reconnect timer failed: %s", esp_err_to_name(err));
    }
}

// The SoftAP only exists as the fallback/config portal: once the station
// holds an IP the portal has done its job. The AP is stopped after a short
// grace period so a provisioning page can show the result first.
static void wlan_ap_stop_timer_cb(void *arg)
{
    sys_wlan_status_t status;
    if (sys_wlan_get_status(&status) != ESP_OK) {
        return;
    }
    // Only take the AP down while the link actually holds; if the station
    // dropped again the AP is the only way back in.
    if (status.state == SYS_WLAN_STATE_CONNECTED && status.ap_active) {
        ESP_LOGI(TAG, "station connected, stopping softap (auto)");
        sys_wlan_ap_stop();
    }
}

static void wlan_arm_ap_stop(void)
{
    if (CONFIG_SYS_WLAN_AP_AUTO_STOP_MS == 0) {
        return;
    }

    bool ap_active;
    wlan_lock();
    ap_active = s_ap_enabled;
    wlan_unlock();
    if (!ap_active) {
        return;
    }

    esp_timer_stop(s_apstop_timer); // restart the grace period on re-connect
    esp_err_t err = esp_timer_start_once(s_apstop_timer, CONFIG_SYS_WLAN_AP_AUTO_STOP_MS * 1000ULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ap auto-stop timer failed: %s", esp_err_to_name(err));
    }
}

static void wlan_on_sta_disconnected(const wifi_event_sta_disconnected_t *info)
{
    bool retry = false;
    bool failed = false;

    wlan_lock();
    s_sta_ip[0] = '\0';
    if (s_want_connect) {
        if (CONFIG_SYS_WLAN_MAX_RETRY == 0 || s_retries < CONFIG_SYS_WLAN_MAX_RETRY) {
            s_retries++;
            s_state = SYS_WLAN_STATE_CONNECTING;
            retry = true;
        } else {
            s_want_connect = false;
            s_state = SYS_WLAN_STATE_DISCONNECTED;
            failed = true;
        }
    } else {
        s_state = s_sta_enabled ? SYS_WLAN_STATE_DISCONNECTED : SYS_WLAN_STATE_STOPPED;
    }
    uint32_t retries = s_retries;
    wlan_unlock();

    xEventGroupClearBits(s_bits, SYS_WLAN_CONNECTED_BIT);

    if (retry) {
        ESP_LOGW(TAG, "sta disconnected (reason=%d), retry %" PRIu32, info->reason, retries);
        wlan_arm_reconnect();
    } else {
        ESP_LOGW(TAG, "sta disconnected (reason=%d)", info->reason);
    }

    wlan_notify(SYS_WLAN_EVENT_DISCONNECTED);

    if (failed) {
        ESP_LOGE(TAG, "giving up after %d attempts", CONFIG_SYS_WLAN_MAX_RETRY);
        xEventGroupSetBits(s_bits, SYS_WLAN_FAILED_BIT);
        wlan_notify(SYS_WLAN_EVENT_CONNECT_FAILED);
    }
}

static void wlan_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START: {
            wlan_lock();
            bool want = s_want_connect;
            wlan_unlock();
            if (want) {
                esp_wifi_connect();
            }
            break;
        }
        case WIFI_EVENT_STA_DISCONNECTED:
            wlan_on_sta_disconnected((const wifi_event_sta_disconnected_t *)data);
            break;
        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "softap started");
            wlan_notify(SYS_WLAN_EVENT_AP_STARTED);
            break;
        case WIFI_EVENT_AP_STOP:
            ESP_LOGI(TAG, "softap stopped");
            wlan_notify(SYS_WLAN_EVENT_AP_STOPPED);
            break;
        case WIFI_EVENT_AP_STACONNECTED:
        case WIFI_EVENT_AP_STADISCONNECTED:
            wlan_notify(SYS_WLAN_EVENT_AP_CLIENT_CHANGED);
            break;
        default:
            break;
        }
        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)data;

        wlan_lock();
        snprintf(s_sta_ip, sizeof(s_sta_ip), IPSTR, IP2STR(&event->ip_info.ip));
        s_state = SYS_WLAN_STATE_CONNECTED;
        s_retries = 0;
        wlan_unlock();

        ESP_LOGI(TAG, "sta got ip " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupClearBits(s_bits, SYS_WLAN_FAILED_BIT);
        xEventGroupSetBits(s_bits, SYS_WLAN_CONNECTED_BIT);
        wlan_notify(SYS_WLAN_EVENT_CONNECTED);
        wlan_arm_ap_stop(); // portal job done, tear the AP down shortly
    }
}

esp_err_t sys_wlan_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t err = sys_nvs_init(); // WiFi calibration data and our own credentials live in NVS
    if (err != ESP_OK) {
        return err;
    }

    s_lock = xSemaphoreCreateMutex();
    s_bits = xEventGroupCreate();
    if (s_lock == NULL || s_bits == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const esp_timer_create_args_t timer_args = {
        .callback = wlan_reconnect_timer_cb,
        .name = "sys_wlan_retry",
    };
    err = esp_timer_create(&timer_args, &s_reconnect_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create failed: %s", esp_err_to_name(err));
        return err;
    }

    const esp_timer_create_args_t apstop_args = {
        .callback = wlan_ap_stop_timer_cb,
        .name = "sys_wlan_apstop",
    };
    err = esp_timer_create(&apstop_args, &s_apstop_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_netif_init();
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) { // another component may own the loop already
        return err;
    }

    s_netif_sta = esp_netif_create_default_wifi_sta();
    if (s_netif_sta == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_netif_set_hostname(s_netif_sta, CONFIG_SYS_WLAN_HOSTNAME);

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wifi_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return err;
    }
    // Credentials are kept in this component's own NVS namespace, so the
    // driver's copy would only be a second source of truth.
    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wlan_event_handler, NULL, NULL);
    if (err == ESP_OK) {
        err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wlan_event_handler, NULL, NULL);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "event handler register failed: %s", esp_err_to_name(err));
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "wlan init done (hostname=%s), radio idle", CONFIG_SYS_WLAN_HOSTNAME);
    return ESP_OK;
}

esp_err_t sys_wlan_connect(const char *ssid, const char *password, bool save)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ssid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t ssid_len = strlen(ssid);
    size_t pass_len = (password != NULL) ? strlen(password) : 0;
    if (ssid_len == 0 || ssid_len > SYS_WLAN_SSID_MAX_LEN || pass_len > SYS_WLAN_PASSWORD_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t cfg = { 0 };
    memcpy(cfg.sta.ssid, ssid, ssid_len);
    if (pass_len > 0) {
        memcpy(cfg.sta.password, password, pass_len);
    }
    cfg.sta.threshold.authmode = (pass_len > 0) ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;

    xEventGroupClearBits(s_bits, SYS_WLAN_CONNECTED_BIT | SYS_WLAN_FAILED_BIT);
    esp_timer_stop(s_reconnect_timer); // drop a retry pending for the previous network

    wlan_lock();
    wlan_copy_str(s_sta_ssid, sizeof(s_sta_ssid), ssid, ssid_len);
    s_sta_ip[0] = '\0';
    s_retries = 0;
    s_want_connect = true;
    s_sta_enabled = true;
    s_state = SYS_WLAN_STATE_CONNECTING;

    esp_err_t err = wlan_sync_mode_locked();
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    }
    bool was_started = false;
    if (err == ESP_OK) {
        err = wlan_sync_power_locked(&was_started);
    }
    if (err != ESP_OK) {
        s_want_connect = false;
        s_state = SYS_WLAN_STATE_DISCONNECTED;
    }
    wlan_unlock();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "connect setup failed: %s", esp_err_to_name(err));
        return err;
    }

    // If the driver was already running there is no STA_START event coming, so
    // kick the association off here instead.
    if (was_started) {
        esp_wifi_disconnect(); // no-op when idle; needed when switching networks
        err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
            return err;
        }
    }

    if (save) {
        esp_err_t save_err = sys_wlan_creds_save(ssid, password);
        if (save_err != ESP_OK) {
            ESP_LOGW(TAG, "saving credentials failed: %s", esp_err_to_name(save_err));
        }
    }

    ESP_LOGI(TAG, "connecting to \"%s\"", ssid);
    wlan_notify(SYS_WLAN_EVENT_CONNECTING);
    return ESP_OK;
}

esp_err_t sys_wlan_connect_saved(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    sys_wlan_creds_t creds;
    esp_err_t err = sys_wlan_creds_load(&creds);
    if (err == ESP_ERR_NVS_NOT_FOUND && strlen(CONFIG_SYS_WLAN_STA_SSID) > 0) {
        ESP_LOGI(TAG, "no stored credentials, using the configured fallback network");
        return sys_wlan_connect(CONFIG_SYS_WLAN_STA_SSID, CONFIG_SYS_WLAN_STA_PASSWORD, false);
    }
    if (err != ESP_OK) {
        return err;
    }
    return sys_wlan_connect(creds.ssid, creds.password, false);
}

esp_err_t sys_wlan_auto_connect(uint32_t timeout_ms, bool ap_fallback)
{
    esp_err_t err = sys_wlan_connect_saved();
    if (err == ESP_OK) {
        err = sys_wlan_wait_connected(timeout_ms);
        if (err == ESP_OK) {
            return ESP_OK;
        }
    }

    if (!ap_fallback) {
        return err;
    }

    ESP_LOGW(TAG, "station not up (%s), starting the softap portal", esp_err_to_name(err));
    const char *ap_pass = (strlen(CONFIG_SYS_WLAN_AP_PASSWORD) > 0) ? CONFIG_SYS_WLAN_AP_PASSWORD : NULL;
    return sys_wlan_ap_start(NULL, ap_pass);
}

esp_err_t sys_wlan_wait_connected(uint32_t timeout_ms)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    EventBits_t bits = xEventGroupWaitBits(s_bits, SYS_WLAN_CONNECTED_BIT | SYS_WLAN_FAILED_BIT,
                                           pdFALSE, pdFALSE, ticks);
    if (bits & SYS_WLAN_CONNECTED_BIT) {
        return ESP_OK;
    }
    if (bits & SYS_WLAN_FAILED_BIT) {
        return ESP_ERR_WIFI_NOT_CONNECT;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t sys_wlan_disconnect(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_timer_stop(s_reconnect_timer);

    wlan_lock();
    s_want_connect = false;
    s_sta_enabled = false;
    s_sta_ip[0] = '\0';
    s_state = SYS_WLAN_STATE_STOPPED;
    bool was_started = s_wifi_started;
    wlan_unlock();

    if (was_started) {
        esp_wifi_disconnect();
    }

    wlan_lock();
    esp_err_t err = wlan_sync_mode_locked();
    if (err == ESP_OK) {
        err = wlan_sync_power_locked(NULL);
    }
    wlan_unlock();

    xEventGroupClearBits(s_bits, SYS_WLAN_CONNECTED_BIT | SYS_WLAN_FAILED_BIT);
    return err;
}

esp_err_t sys_wlan_ap_start(const char *ssid, const char *password)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_timer_stop(s_apstop_timer); // a pending auto-stop must not kill a fresh AP

    size_t pass_len = (password != NULL) ? strlen(password) : 0;
    if (pass_len > 0 && (pass_len < 8 || pass_len > SYS_WLAN_PASSWORD_MAX_LEN)) {
        return ESP_ERR_INVALID_ARG; // WPA2 PSK is 8..64 characters
    }

    char name[SYS_WLAN_SSID_MAX_LEN + 1];
    if (ssid != NULL && ssid[0] != '\0') {
        size_t ssid_len = strlen(ssid);
        if (ssid_len > SYS_WLAN_SSID_MAX_LEN) {
            return ESP_ERR_INVALID_ARG;
        }
        wlan_copy_str(name, sizeof(name), ssid, ssid_len);
    } else {
        uint8_t mac[6] = { 0 };
        esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
        snprintf(name, sizeof(name), "%s-%02X%02X", CONFIG_SYS_WLAN_AP_SSID, mac[4], mac[5]);
    }

    if (s_netif_ap == NULL) {
        s_netif_ap = esp_netif_create_default_wifi_ap();
        if (s_netif_ap == NULL) {
            return ESP_ERR_NO_MEM;
        }
        esp_netif_set_hostname(s_netif_ap, CONFIG_SYS_WLAN_HOSTNAME);
    }

    size_t name_len = strlen(name);
    wifi_config_t cfg = { 0 };
    memcpy(cfg.ap.ssid, name, name_len);
    cfg.ap.ssid_len = (uint8_t)name_len;
    cfg.ap.channel = CONFIG_SYS_WLAN_AP_CHANNEL;
    cfg.ap.max_connection = CONFIG_SYS_WLAN_AP_MAX_CONN;
    cfg.ap.authmode = (pass_len > 0) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    if (pass_len > 0) {
        memcpy(cfg.ap.password, password, pass_len);
    }

    wlan_lock();
    bool was_enabled = s_ap_enabled;
    s_ap_enabled = true;
    wlan_copy_str(s_ap_ssid, sizeof(s_ap_ssid), name, name_len);

    // Mode before config: the driver only accepts an AP config once the AP
    // interface exists, and only beacons once the radio is started.
    esp_err_t err = wlan_sync_mode_locked();
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_AP, &cfg);
    }
    if (err == ESP_OK) {
        err = wlan_sync_power_locked(NULL);
    }
    if (err != ESP_OK) {
        s_ap_enabled = was_enabled;
    }
    wlan_unlock();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "softap start failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "softap \"%s\" up (%s, channel %d)", name,
             (pass_len > 0) ? "wpa2" : "open", CONFIG_SYS_WLAN_AP_CHANNEL);
    return ESP_OK;
}

esp_err_t sys_wlan_ap_stop(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_timer_stop(s_apstop_timer); // the AP is going down now, nothing left to arm

    wlan_lock();
    s_ap_enabled = false;
    s_ap_ssid[0] = '\0';
    esp_err_t err = wlan_sync_mode_locked();
    if (err == ESP_OK) {
        err = wlan_sync_power_locked(NULL);
    }
    wlan_unlock();

    return err;
}

// The API contract promises strongest-first; the driver's raw list is not
// sorted, so sort it before handing it out.
static int wlan_scan_sort_cmp(const void *a, const void *b)
{
    const wifi_ap_record_t *ra = (const wifi_ap_record_t *)a;
    const wifi_ap_record_t *rb = (const wifi_ap_record_t *)b;
    return rb->rssi - ra->rssi;
}

esp_err_t sys_wlan_scan(sys_wlan_ap_t *out, size_t max, size_t *out_count)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (out == NULL || out_count == NULL || max == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;

    wifi_ap_record_t *records = calloc(max, sizeof(wifi_ap_record_t));
    if (records == NULL) {
        return ESP_ERR_NO_MEM;
    }

    wlan_lock();
    if (s_scanning) {
        wlan_unlock();
        free(records);
        return ESP_ERR_INVALID_STATE;
    }
    s_scanning = true;
    // A scan needs the station interface; if nothing else is using it, bring it
    // up for the duration and put it back afterwards.
    bool sta_was_enabled = s_sta_enabled;
    s_sta_enabled = true;
    esp_err_t err = wlan_sync_mode_locked();
    if (err == ESP_OK) {
        err = wlan_sync_power_locked(NULL);
    }
    wlan_unlock();

    if (err == ESP_OK) {
        wifi_scan_config_t scan_cfg = { .show_hidden = false };
        err = esp_wifi_scan_start(&scan_cfg, true); // blocking
    }

    if (err == ESP_OK) {
        // A successful get_ap_records() also frees the driver's list; on
        // failure we have to drop it ourselves or the next scan leaks it.
        uint16_t num = (max > UINT16_MAX) ? UINT16_MAX : (uint16_t)max;
        err = esp_wifi_scan_get_ap_records(&num, records);
        if (err == ESP_OK) {
            qsort(records, num, sizeof(records[0]), wlan_scan_sort_cmp);
            for (uint16_t i = 0; i < num; i++) {
                wlan_copy_str(out[i].ssid, sizeof(out[i].ssid), (const char *)records[i].ssid,
                              strnlen((const char *)records[i].ssid, SYS_WLAN_SSID_MAX_LEN));
                out[i].rssi = records[i].rssi;
                out[i].channel = records[i].primary;
                out[i].authmode = records[i].authmode;
            }
            *out_count = num;
        } else {
            esp_wifi_clear_ap_list();
        }
    }

    free(records);

    wlan_lock();
    s_scanning = false;
    if (!sta_was_enabled) {
        s_sta_enabled = false;
        wlan_sync_mode_locked();
        wlan_sync_power_locked(NULL);
    }
    wlan_unlock();

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "scan found %u networks", (unsigned)*out_count);
    return ESP_OK;
}

esp_err_t sys_wlan_get_status(sys_wlan_status_t *out_status)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (out_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_status, 0, sizeof(*out_status));

    wlan_lock();
    out_status->state = s_state;
    out_status->ap_active = s_ap_enabled;
    memcpy(out_status->sta_ssid, s_sta_ssid, sizeof(out_status->sta_ssid));
    memcpy(out_status->sta_ip, s_sta_ip, sizeof(out_status->sta_ip));
    memcpy(out_status->ap_ssid, s_ap_ssid, sizeof(out_status->ap_ssid));
    bool connected = (s_state == SYS_WLAN_STATE_CONNECTED);
    bool ap_active = s_ap_enabled;
    wlan_unlock();

    if (connected) {
        wifi_ap_record_t record;
        if (esp_wifi_sta_get_ap_info(&record) == ESP_OK) {
            out_status->sta_rssi = record.rssi;
        }
    }

    if (ap_active && s_netif_ap != NULL) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(s_netif_ap, &ip_info) == ESP_OK) {
            snprintf(out_status->ap_ip, sizeof(out_status->ap_ip), IPSTR, IP2STR(&ip_info.ip));
        }
        wifi_sta_list_t clients;
        if (esp_wifi_ap_get_sta_list(&clients) == ESP_OK) {
            out_status->ap_clients = (uint8_t)clients.num;
        }
    }

    return ESP_OK;
}

bool sys_wlan_is_connected(void)
{
    if (!s_initialized) {
        return false;
    }
    return (xEventGroupGetBits(s_bits) & SYS_WLAN_CONNECTED_BIT) != 0;
}

esp_netif_t *sys_wlan_get_sta_netif(void)
{
    return s_netif_sta;
}

esp_netif_t *sys_wlan_get_ap_netif(void)
{
    return s_netif_ap;
}

esp_err_t sys_wlan_register_event_cb(sys_wlan_event_cb_t cb, void *ctx)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (cb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ESP_ERR_NO_MEM;
    wlan_lock();
    for (size_t i = 0; i < CONFIG_SYS_WLAN_MAX_EVENT_CBS; i++) {
        if (s_cbs[i].cb == cb && s_cbs[i].ctx == ctx) {
            err = ESP_OK; // already registered, keep it a single entry
            break;
        }
        if (s_cbs[i].cb == NULL) {
            s_cbs[i].cb = cb;
            s_cbs[i].ctx = ctx;
            err = ESP_OK;
            break;
        }
    }
    wlan_unlock();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "event callback table full (%d entries)", CONFIG_SYS_WLAN_MAX_EVENT_CBS);
    }
    return err;
}

esp_err_t sys_wlan_unregister_event_cb(sys_wlan_event_cb_t cb, void *ctx)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_ERR_NOT_FOUND;
    wlan_lock();
    for (size_t i = 0; i < CONFIG_SYS_WLAN_MAX_EVENT_CBS; i++) {
        if (s_cbs[i].cb == cb && s_cbs[i].ctx == ctx) {
            s_cbs[i].cb = NULL;
            s_cbs[i].ctx = NULL;
            err = ESP_OK;
            break;
        }
    }
    wlan_unlock();
    return err;
}
