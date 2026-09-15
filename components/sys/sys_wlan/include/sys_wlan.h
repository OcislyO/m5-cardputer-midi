#pragma once

#include "esp_err.h"
#include "esp_netif.h"
#include "esp_wifi_types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SYS_WLAN_SSID_MAX_LEN     32 // 802.11 SSID, without the terminator
#define SYS_WLAN_PASSWORD_MAX_LEN 64 // WPA2 PSK, without the terminator
#define SYS_WLAN_IP_STR_LEN       16 // "255.255.255.255" + terminator

/** @brief STA link state. The SoftAP runs independently, see sys_wlan_status_t::ap_active. */
typedef enum {
    SYS_WLAN_STATE_STOPPED = 0,  // STA interface not in use
    SYS_WLAN_STATE_CONNECTING,   // association / DHCP in progress (includes retries)
    SYS_WLAN_STATE_CONNECTED,    // associated and holding an IP
    SYS_WLAN_STATE_DISCONNECTED, // STA enabled but not associated (retries exhausted or asked to stop)
} sys_wlan_state_t;

/** @brief Notifications delivered to callbacks registered with sys_wlan_register_event_cb(). */
typedef enum {
    SYS_WLAN_EVENT_CONNECTING = 0,
    SYS_WLAN_EVENT_CONNECTED,      // got an IP -- servers can bind now
    SYS_WLAN_EVENT_DISCONNECTED,   // link dropped; a retry may follow
    SYS_WLAN_EVENT_CONNECT_FAILED, // retries exhausted, no further attempt without a new call
    SYS_WLAN_EVENT_AP_STARTED,
    SYS_WLAN_EVENT_AP_STOPPED,
    SYS_WLAN_EVENT_AP_CLIENT_CHANGED, // a station joined or left the SoftAP
} sys_wlan_event_t;

/**
 * @brief Event callback. Runs on the system event task, so it must not block;
 *        push to a queue instead of doing work inline.
 */
typedef void (*sys_wlan_event_cb_t)(sys_wlan_event_t event, void *ctx);

/** @brief One scan result, as a web/UI layer wants to show it. */
typedef struct {
    char ssid[SYS_WLAN_SSID_MAX_LEN + 1];
    int8_t rssi;
    uint8_t channel;
    wifi_auth_mode_t authmode; // WIFI_AUTH_OPEN means no password needed
} sys_wlan_ap_t;

/** @brief Stored station credentials. */
typedef struct {
    char ssid[SYS_WLAN_SSID_MAX_LEN + 1];
    char password[SYS_WLAN_PASSWORD_MAX_LEN + 1];
} sys_wlan_creds_t;

/** @brief Everything a status page/LCD needs in one snapshot. */
typedef struct {
    sys_wlan_state_t state;
    char sta_ssid[SYS_WLAN_SSID_MAX_LEN + 1]; // SSID last asked to connect to, "" if none
    char sta_ip[SYS_WLAN_IP_STR_LEN];         // "" unless state is CONNECTED
    int8_t sta_rssi;                          // 0 unless state is CONNECTED
    bool ap_active;
    char ap_ssid[SYS_WLAN_SSID_MAX_LEN + 1];
    char ap_ip[SYS_WLAN_IP_STR_LEN];
    uint8_t ap_clients;
} sys_wlan_status_t;

/**
 * @brief Bring up the WiFi stack: NVS, netif, the default event loop and the
 *        WiFi driver, plus the STA netif and this component's event handlers.
 *
 * Idempotent. Leaves the radio idle -- nothing transmits until
 * sys_wlan_connect(), sys_wlan_ap_start() or sys_wlan_scan() is called.
 */
esp_err_t sys_wlan_init(void);

/**
 * @brief Start connecting (and auto-reconnecting) to an access point.
 *
 * Returns as soon as the attempt is started; wait for the result with
 * sys_wlan_wait_connected() or a registered callback. Reconnects are retried
 * CONFIG_SYS_WLAN_MAX_RETRY times (0 = forever) before the link is reported as
 * failed. A SoftAP that is already running keeps running (mode becomes APSTA).
 *
 * @param ssid     Network name, 1..SYS_WLAN_SSID_MAX_LEN characters.
 * @param password NULL or "" for an open network.
 * @param save     Also store the credentials in NVS for sys_wlan_connect_saved().
 */
esp_err_t sys_wlan_connect(const char *ssid, const char *password, bool save);

/**
 * @brief Connect using the credentials stored by sys_wlan_creds_save(),
 *        falling back to CONFIG_SYS_WLAN_STA_SSID when NVS holds none.
 * @return ESP_ERR_NVS_NOT_FOUND if neither is available.
 */
esp_err_t sys_wlan_connect_saved(void);

/**
 * @brief Connect to the saved network and, if that does not come up in time,
 *        optionally start the SoftAP so a browser can supply new credentials.
 *
 * The provisioning flow in one call: on return either the STA holds an IP or
 * (with ap_fallback) the config portal AP is up.
 *
 * @param timeout_ms  How long to wait for an IP, 0 waits forever.
 * @param ap_fallback Start the SoftAP if the station does not connect.
 */
esp_err_t sys_wlan_auto_connect(uint32_t timeout_ms, bool ap_fallback);

/**
 * @brief Block until the station holds an IP.
 * @param timeout_ms 0 waits forever.
 * @return ESP_OK connected, ESP_ERR_TIMEOUT still trying,
 *         ESP_ERR_WIFI_NOT_CONNECT retries exhausted.
 */
esp_err_t sys_wlan_wait_connected(uint32_t timeout_ms);

/** @brief Drop the station link and stop auto-reconnecting (the SoftAP is untouched). */
esp_err_t sys_wlan_disconnect(void);

/**
 * @brief Start the SoftAP, e.g. to serve a configuration page when no network
 *        is reachable.
 * @param ssid     NULL/"" for CONFIG_SYS_WLAN_AP_SSID with a MAC suffix.
 * @param password NULL/"" for an open AP, otherwise at least 8 characters (WPA2).
 */
esp_err_t sys_wlan_ap_start(const char *ssid, const char *password);

/** @brief Stop the SoftAP. The station link, if any, is untouched. */
esp_err_t sys_wlan_ap_stop(void);

/**
 * @brief Scan for access points (blocking, a few seconds).
 *
 * Enables the station interface for the duration if it was idle. Safe while
 * connected -- the link is kept.
 *
 * @param out       Caller's array, filled strongest-first.
 * @param max       Capacity of @p out.
 * @param out_count Number of entries written.
 */
esp_err_t sys_wlan_scan(sys_wlan_ap_t *out, size_t max, size_t *out_count);

/** @brief Snapshot of the current link/AP state, for a status page or the LCD. */
esp_err_t sys_wlan_get_status(sys_wlan_status_t *out_status);

/** @brief Shorthand for "the station holds an IP", e.g. to gate starting a server. */
bool sys_wlan_is_connected(void);

/** @brief STA netif handle, NULL before sys_wlan_init(). For mDNS/socket binding. */
esp_netif_t *sys_wlan_get_sta_netif(void);

/** @brief SoftAP netif handle, NULL until sys_wlan_ap_start() has been called. */
esp_netif_t *sys_wlan_get_ap_netif(void);

/**
 * @brief Register a callback for link/AP changes.
 *        Up to CONFIG_SYS_WLAN_MAX_EVENT_CBS callbacks; see sys_wlan_event_cb_t
 *        for the context it runs in.
 */
esp_err_t sys_wlan_register_event_cb(sys_wlan_event_cb_t cb, void *ctx);

/** @brief Remove a callback registered with the same (cb, ctx) pair. */
esp_err_t sys_wlan_unregister_event_cb(sys_wlan_event_cb_t cb, void *ctx);

/** @brief Store station credentials in NVS (survives reboot). */
esp_err_t sys_wlan_creds_save(const char *ssid, const char *password);

/**
 * @brief Read the stored credentials.
 * @return ESP_ERR_NVS_NOT_FOUND if nothing has been saved.
 */
esp_err_t sys_wlan_creds_load(sys_wlan_creds_t *out_creds);

/** @brief Forget the stored credentials. ESP_OK even if there were none. */
esp_err_t sys_wlan_creds_erase(void);

#ifdef __cplusplus
}
#endif
