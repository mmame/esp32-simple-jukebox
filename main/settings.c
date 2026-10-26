#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <string.h>

#include "settings.h"
#include "wifi_ap.h"

static const char *TAG = "settings";

#define NAMESPACE   "player"

static volatile int s_dim_s = SETTINGS_DIM_S_DEFAULT;
static volatile int s_dim_level = SETTINGS_DIM_LEVEL_DEFAULT;

static volatile int s_batt_cal = SETTINGS_BATT_CAL_DEFAULT;
static volatile int s_eq_bass = SETTINGS_EQ_DEFAULT_BASS;
static volatile int s_eq_treble = SETTINGS_EQ_DEFAULT_TREBLE;
static char s_ssid[SETTINGS_WIFI_SSID_MAX + 1] = WIFI_AP_SSID;
static char s_pass[SETTINGS_WIFI_PASS_MAX + 1] = WIFI_AP_PASSWORD;

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
    if (nvs_get_i32(h, "batt_cal", &v) == ESP_OK)
        s_batt_cal = clamp(v, SETTINGS_BATT_CAL_MIN, SETTINGS_BATT_CAL_MAX);
    if (nvs_get_i32(h, "eq_bass", &v) == ESP_OK)
        s_eq_bass = clamp(v, 0, SETTINGS_EQ_MAX);
    if (nvs_get_i32(h, "eq_treble", &v) == ESP_OK)
        s_eq_treble = clamp(v, 0, SETTINGS_EQ_MAX);
    char text[SETTINGS_WIFI_PASS_MAX + 1];
    size_t len = sizeof(text);
    if (nvs_get_str(h, "wifi_ssid", text, &len) == ESP_OK && len > 1)
    {
        len = sizeof(text);
        char pass[SETTINGS_WIFI_PASS_MAX + 1];
        if (nvs_get_str(h, "wifi_pass", pass, &len) == ESP_OK && strlen(pass) >= SETTINGS_WIFI_PASS_MIN)
        {
            strlcpy(s_ssid, text, sizeof(s_ssid));
            strlcpy(s_pass, pass, sizeof(s_pass));
        }
    }
    nvs_close(h);
    ESP_LOGI(TAG, "dim after %d s, dimmed level %d, battery calibration %d", s_dim_s, s_dim_level, s_batt_cal);
}

int settings_batt_cal(void)
{
    return s_batt_cal;
}

esp_err_t settings_set_batt_cal(int cal)
{
    cal = clamp(cal, SETTINGS_BATT_CAL_MIN, SETTINGS_BATT_CAL_MAX);
    s_batt_cal = cal;

    nvs_handle_t h;
    esp_err_t ret = nvs_open(NAMESPACE, NVS_READWRITE, &h);
    if (ret != ESP_OK)
        return ret;
    ret = nvs_set_i32(h, "batt_cal", cal);
    if (ret == ESP_OK)
        ret = nvs_commit(h);
    nvs_close(h);
    return ret;
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

const char *settings_wifi_ssid(void)
{
    return s_ssid;
}

const char *settings_wifi_password(void)
{
    return s_pass;
}

esp_err_t settings_set_wifi(const char *ssid, const char *password)
{
    size_t sl = strlen(ssid), pl = strlen(password);
    if (sl < 1 || sl > SETTINGS_WIFI_SSID_MAX || pl < SETTINGS_WIFI_PASS_MIN || pl > SETTINGS_WIFI_PASS_MAX)
        return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    esp_err_t ret = nvs_open(NAMESPACE, NVS_READWRITE, &h);
    if (ret != ESP_OK)
        return ret;
    ret = nvs_set_str(h, "wifi_ssid", ssid);
    if (ret == ESP_OK)
        ret = nvs_set_str(h, "wifi_pass", password);
    if (ret == ESP_OK)
        ret = nvs_commit(h);
    nvs_close(h);
    if (ret == ESP_OK)      /* the running access point keeps its old credentials until the WiFi is switched on again */
    {
        strlcpy(s_ssid, ssid, sizeof(s_ssid));
        strlcpy(s_pass, password, sizeof(s_pass));
    }
    return ret;
}

int settings_eq_bass(void)
{
    return s_eq_bass;
}

int settings_eq_treble(void)
{
    return s_eq_treble;
}

esp_err_t settings_set_eq(int bass, int treble)
{
    bass = clamp(bass, 0, SETTINGS_EQ_MAX);
    treble = clamp(treble, 0, SETTINGS_EQ_MAX);
    s_eq_bass = bass;
    s_eq_treble = treble;

    nvs_handle_t h;
    esp_err_t ret = nvs_open(NAMESPACE, NVS_READWRITE, &h);
    if (ret != ESP_OK)
        return ret;
    ret = nvs_set_i32(h, "eq_bass", bass);
    if (ret == ESP_OK)
        ret = nvs_set_i32(h, "eq_treble", treble);
    if (ret == ESP_OK)
        ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}
