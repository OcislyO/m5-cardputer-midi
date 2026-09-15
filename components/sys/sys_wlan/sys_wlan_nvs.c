// Credential storage for sys_wlan: its own NVS namespace, so the web layer can
// save what the user typed and the next boot can come up without a portal.
#include "sys_wlan.h"
#include "esp_log.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "sys_wlan";

#define SYS_WLAN_NVS_NAMESPACE "sys_wlan"
#define SYS_WLAN_NVS_KEY_SSID  "ssid"
#define SYS_WLAN_NVS_KEY_PASS  "pass"

esp_err_t sys_wlan_creds_save(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0' || strlen(ssid) > SYS_WLAN_SSID_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    if (password != NULL && strlen(password) > SYS_WLAN_PASSWORD_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SYS_WLAN_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(handle, SYS_WLAN_NVS_KEY_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, SYS_WLAN_NVS_KEY_PASS, (password != NULL) ? password : "");
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saving credentials failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "credentials for \"%s\" saved", ssid);
    }
    return err;
}

esp_err_t sys_wlan_creds_load(sys_wlan_creds_t *out_creds)
{
    if (out_creds == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_creds, 0, sizeof(*out_creds));

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SYS_WLAN_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        // A namespace that was never written reads back as "not found", which
        // is the same answer callers want for "nothing saved yet".
        return (err == ESP_ERR_NVS_NOT_FOUND) ? ESP_ERR_NVS_NOT_FOUND : err;
    }

    size_t len = sizeof(out_creds->ssid);
    err = nvs_get_str(handle, SYS_WLAN_NVS_KEY_SSID, out_creds->ssid, &len);
    if (err == ESP_OK) {
        len = sizeof(out_creds->password);
        esp_err_t pass_err = nvs_get_str(handle, SYS_WLAN_NVS_KEY_PASS, out_creds->password, &len);
        if (pass_err == ESP_ERR_NVS_NOT_FOUND) {
            out_creds->password[0] = '\0'; // open network
        } else if (pass_err != ESP_OK) {
            err = pass_err;
        }
    }
    nvs_close(handle);

    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "loading credentials failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t sys_wlan_creds_erase(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SYS_WLAN_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return (err == ESP_ERR_NVS_NOT_FOUND) ? ESP_OK : err;
    }

    err = nvs_erase_all(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK; // nothing stored, which is the state the caller asked for
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "stored credentials erased");
    }
    return err;
}
