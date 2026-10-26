#include <string.h>

#include "esp_log.h"

#include "player.h"
#include "sd_card.h"
#include "settings.h"
#include "upload_mode.h"
#include "web_server.h"
#include "wifi_ap.h"

static const char *TAG = "upload_mode";

esp_err_t upload_mode_start(upload_info_t *info, void (*on_done)(void))
{
    memset(info, 0, sizeof(*info));
    player_reset();     /* audio and the SD card bus are needed for the upload, and the album list gets replaced */

    esp_err_t ret = sd_card_mount();    /* no-op if already mounted, retries if the card was missing at boot */
    if (ret != ESP_OK)
        return ret;
    ret = wifi_ap_start();
    if (ret != ESP_OK)
        return ret;
    ret = web_server_start(on_done);
    if (ret != ESP_OK)
    {
        wifi_ap_stop();
        return ret;
    }
    strlcpy(info->ssid, settings_wifi_ssid(), sizeof(info->ssid));
    strlcpy(info->password, settings_wifi_password(), sizeof(info->password));
    strlcpy(info->url, WIFI_AP_URL, sizeof(info->url));
    ESP_LOGI(TAG, "upload mode on: %s / %s", info->ssid, info->url);
    return ESP_OK;
}

void upload_mode_stop(void)
{
    web_server_stop();
    wifi_ap_stop();
    ESP_LOGI(TAG, "upload mode off");
}
