#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "settings.h"

static const char *TAG = "settings";

#define NAMESPACE   "player"

static volatile int s_dim_s = SETTINGS_DIM_S_DEFAULT;
static volatile int s_dim_level = SETTINGS_DIM_LEVEL_DEFAULT;

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void settings_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "NVS not available (%s), using the defaults", esp_err_to_name(ret));
        return;
    }
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READONLY, &h) != ESP_OK)
        return;      /* nothing stored yet */
    int32_t v;
    if (nvs_get_i32(h, "dim_s", &v) == ESP_OK)
        s_dim_s = clamp(v, 0, SETTINGS_DIM_S_MAX);
    if (nvs_get_i32(h, "dim_level", &v) == ESP_OK)
        s_dim_level = clamp(v, SETTINGS_DIM_LEVEL_MIN, SETTINGS_DIM_LEVEL_MAX);
    nvs_close(h);
    ESP_LOGI(TAG, "dim after %d s, dimmed level %d", s_dim_s, s_dim_level);
}

int settings_dim_timeout_s(void)
{
    return s_dim_s;
}

int settings_dim_level(void)
{
    return s_dim_level;
}

esp_err_t settings_set_dim(int timeout_s, int level)
{
    timeout_s = clamp(timeout_s, 0, SETTINGS_DIM_S_MAX);
    level = clamp(level, SETTINGS_DIM_LEVEL_MIN, SETTINGS_DIM_LEVEL_MAX);
    s_dim_s = timeout_s;
    s_dim_level = level;

    nvs_handle_t h;
    esp_err_t ret = nvs_open(NAMESPACE, NVS_READWRITE, &h);
    if (ret != ESP_OK)
        return ret;
    ret = nvs_set_i32(h, "dim_s", timeout_s);
    if (ret == ESP_OK)
        ret = nvs_set_i32(h, "dim_level", level);
    if (ret == ESP_OK)
        ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}
