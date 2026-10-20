#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "wifi_ap.h"

static const char *TAG = "wifi_ap";

static esp_netif_t *s_netif;
static bool s_running;

esp_err_t wifi_ap_start(void)
{
    if (s_running)
        return ESP_ERR_INVALID_STATE;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
        return ret;

    s_netif = esp_netif_create_default_wifi_ap();
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&init_cfg);
    if (ret != ESP_OK)
    {
        esp_netif_destroy_default_wifi(s_netif);
        return ret;
    }
    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.ap.ssid, WIFI_AP_SSID, sizeof(cfg.ap.ssid));
    strlcpy((char *)cfg.ap.password, WIFI_AP_PASSWORD, sizeof(cfg.ap.password));
    cfg.ap.ssid_len = strlen(WIFI_AP_SSID);
    cfg.ap.channel = 6;
    cfg.ap.max_connection = 4;
    cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);
    s_running = true;
    ESP_LOGI(TAG, "AP \"%s\" up, free heap %u", WIFI_AP_SSID, (unsigned)esp_get_free_heap_size());
    return ESP_OK;
}

void wifi_ap_stop(void)
{
    if (!s_running)
        return;
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_netif_destroy_default_wifi(s_netif);
    s_netif = NULL;
    s_running = false;
    ESP_LOGI(TAG, "AP stopped, free heap %u", (unsigned)esp_get_free_heap_size());
}
