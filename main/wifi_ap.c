#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "settings.h"
#include "wifi_ap.h"

static const char *TAG = "wifi_ap";

static esp_netif_t *s_netif;
static bool s_running;

/* Who joins / leaves, with the reason code (200+ = beacon timeout, 8 = leaving, 15 = handshake timeout, ...). */
static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == WIFI_EVENT_AP_STACONNECTED)
    {
        const wifi_event_ap_staconnected_t *e = data;
        ESP_LOGI(TAG, "station " MACSTR " joined", MAC2STR(e->mac));
    }
    else if (id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        const wifi_event_ap_stadisconnected_t *e = data;
        ESP_LOGW(TAG, "station " MACSTR " left, reason %d", MAC2STR(e->mac), e->reason);
    }
}

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
    strlcpy((char *)cfg.ap.ssid, settings_wifi_ssid(), sizeof(cfg.ap.ssid));
    strlcpy((char *)cfg.ap.password, settings_wifi_password(), sizeof(cfg.ap.password));
    cfg.ap.ssid_len = strlen(settings_wifi_ssid());
    cfg.ap.channel = 6;
    cfg.ap.max_connection = 4;
    cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);
    /* 20 MHz instead of the default 40 MHz channel: much less sensitive to interference and weak signal */
    esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL);
    s_running = true;
    ESP_LOGI(TAG, "AP \"%s\" up, free heap %u", settings_wifi_ssid(), (unsigned)esp_get_free_heap_size());
    return ESP_OK;
}

void wifi_ap_stop(void)
{
    if (!s_running)
        return;
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event);
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_netif_destroy_default_wifi(s_netif);
    s_netif = NULL;
    s_running = false;
    ESP_LOGI(TAG, "AP stopped, free heap %u", (unsigned)esp_get_free_heap_size());
}
