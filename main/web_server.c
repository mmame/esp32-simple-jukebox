#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sd_card.h"
#include "settings.h"
#include "volume_pot.h"
#include "web_server.h"
#include "screenshot.h"

static const char *TAG = "web_server";

#define NAME_MAX_LEN    96
#define IO_BUF_SIZE     8192

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t lame_js_start[] asm("_binary_lame_min_js_start");
extern const uint8_t lame_js_end[] asm("_binary_lame_min_js_end");

static httpd_handle_t s_server;
static void (*s_on_done)(void);

/* ---------- helpers ---------- */

static void url_decode(char *s)
{
    char *o = s;
    while (*s)
    {
        if (*s == '%' && s[1] && s[2])
        {
            char hex[3] = { s[1], s[2], 0 };
            *o++ = (char)strtol(hex, NULL, 16);
            s += 3;
        }
        else
        {
            *o++ = (*s == '+') ? ' ' : *s;
            s++;
        }
    }
    *o = 0;
}

/* Reads and decodes a query parameter; false if missing or too long. */
static bool get_param(httpd_req_t *req, const char *key, char *out, size_t out_size)
{
    char query[1100];
    char raw[3 * NAME_MAX_LEN + 16];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK)
        return false;
    if (httpd_query_key_value(query, key, raw, sizeof(raw)) != ESP_OK)
        return false;
    url_decode(raw);
    if (strlen(raw) >= out_size)
        return false;
    strcpy(out, raw);
    return true;
}

/* A single FAT-safe path component: no separators, no reserved characters, no leading dot. */
static bool valid_name(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n > NAME_MAX_LEN || s[0] == '.' || s[n - 1] == ' ' || s[n - 1] == '.')
        return false;
    for (const char *p = s; *p; p++)
    {
        if ((unsigned char)*p < 0x20 || strchr("\\/:*?\"<>|", *p))
            return false;
    }
    return true;
}

static bool valid_file(const char *s)
{
    size_t n = strlen(s);
    return valid_name(s) && (strcasecmp(s, "cover.jpg") == 0 || (n > 4 && strcasecmp(s + n - 4, ".mp3") == 0));
}

static esp_err_t bad_request(httpd_req_t *req, const char *msg)
{
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, msg);
    return ESP_FAIL;
}

static esp_err_t send_ok(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

/* ---------- static pages ---------- */

static esp_err_t page_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);
}

static esp_err_t page_lame(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    return httpd_resp_send(req, (const char *)lame_js_start, lame_js_end - lame_js_start);
}

/* ---------- library ---------- */

/* The library list is streamed in small chunks. Building a cJSON tree instead (a few hundred bytes per track) used up the
 * internal RAM with a big library and crashed the player while the page was loading. */
typedef struct {
    httpd_req_t *req;
    char buf[1024];
    size_t len;
    bool err;
} jout_t;

static void jflush(jout_t *j)
{
    if (j->len && !j->err && httpd_resp_send_chunk(j->req, j->buf, j->len) != ESP_OK)
        j->err = true;
    j->len = 0;
}

static void jraw(jout_t *j, const char *s)
{
    for (; *s; s++)
    {
        j->buf[j->len++] = *s;
        if (j->len == sizeof(j->buf))
            jflush(j);
    }
}

static void jstr(jout_t *j, const char *s)
{
    char c2[2] = { 0, 0 };
    jraw(j, "\"");
    for (; *s; s++)
    {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\')
        {
            jraw(j, "\\");
            c2[0] = (char)c;
            jraw(j, c2);
        }
        else if (c < 0x20)
        {
            char e[8];
            snprintf(e, sizeof(e), "\\u%04x", c);
            jraw(j, e);
        }
        else
        {
            c2[0] = (char)c;
            jraw(j, c2);
        }
    }
    jraw(j, "\"");
}

static esp_err_t api_albums(httpd_req_t *req)
{
    jout_t *j = malloc(sizeof(jout_t));
    if (!j)
        return httpd_resp_send_500(req);
    j->req = req;
    j->len = 0;
    j->err = false;
    httpd_resp_set_type(req, "application/json");

    jraw(j, "{\"albums\":[");
    DIR *dir = opendir(SD_MOUNT_POINT);
    struct dirent *e;
    bool first_album = true;
    while (dir && (e = readdir(dir)) != NULL && !j->err)
    {
        if (e->d_type != DT_DIR || e->d_name[0] == '.' || strcmp(e->d_name, "System Volume Information") == 0)
            continue;

        if (!first_album)
            jraw(j, ",");
        first_album = false;
        jraw(j, "{\"name\":");
        jstr(j, e->d_name);
        jraw(j, ",\"tracks\":[");

        bool cover = false, first_track = true;
        char path[272];     /* mount point + longest FAT name */
        snprintf(path, sizeof(path), SD_MOUNT_POINT "/%s", e->d_name);
        DIR *ad = opendir(path);
        struct dirent *f;
        while (ad && (f = readdir(ad)) != NULL && !j->err)
        {
            size_t n = strlen(f->d_name);
            if (strcasecmp(f->d_name, "cover.jpg") == 0)
            {
                cover = true;
            }
            else if (n > 4 && strcasecmp(f->d_name + n - 4, ".mp3") == 0 && f->d_name[0] != '.')
            {
                char fp[560];
                char num[48];
                struct stat st;
                snprintf(fp, sizeof(fp), "%s/%s", path, f->d_name);
                if (!first_track)
                    jraw(j, ",");
                first_track = false;
                jraw(j, "{\"name\":");
                jstr(j, f->d_name);
                snprintf(num, sizeof(num), ",\"size\":%lu}", stat(fp, &st) == 0 ? (unsigned long)st.st_size : 0UL);
                jraw(j, num);
            }
        }
        if (ad)
            closedir(ad);
        jraw(j, cover ? "],\"cover\":true}" : "],\"cover\":false}");
    }
    if (dir)
        closedir(dir);
    jraw(j, "]");

    uint64_t total = 0, free_bytes = 0;
    if (esp_vfs_fat_info(SD_MOUNT_POINT, &total, &free_bytes) == ESP_OK)
    {
        char num[96];
        snprintf(num, sizeof(num), ",\"total\":%llu,\"free\":%llu", (unsigned long long)total, (unsigned long long)free_bytes);
        jraw(j, num);
    }
    jraw(j, ",\"version\":");
    jstr(j, esp_app_get_description()->version);
    jraw(j, "}");
    jflush(j);

    esp_err_t ret = j->err ? ESP_FAIL : httpd_resp_send_chunk(req, NULL, 0);
    free(j);
    return ret;
}

static esp_err_t api_cover(httpd_req_t *req)
{
    char album[NAME_MAX_LEN + 4], path[320];
    if (!get_param(req, "album", album, sizeof(album)) || !valid_name(album))
        return bad_request(req, "bad album");
    snprintf(path, sizeof(path), SD_MOUNT_POINT "/%s/cover.jpg", album);
    FILE *f = fopen(path, "rb");
    if (!f)
        return httpd_resp_send_404(req);

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    char *buf = malloc(IO_BUF_SIZE);
    if (!buf)
    {
        fclose(f);
        return httpd_resp_send_500(req);
    }
    size_t n;
    esp_err_t ret = ESP_OK;
    while (ret == ESP_OK && (n = fread(buf, 1, IO_BUF_SIZE, f)) > 0)
        ret = httpd_resp_send_chunk(req, buf, n);
    if (ret == ESP_OK)
        ret = httpd_resp_send_chunk(req, NULL, 0);
    free(buf);
    fclose(f);
    return ret;
}

static esp_err_t api_file_put(httpd_req_t *req)
{
    char album[NAME_MAX_LEN + 4], file[NAME_MAX_LEN + 4];
    if (!get_param(req, "album", album, sizeof(album)) || !valid_name(album) ||
        !get_param(req, "file", file, sizeof(file)) || !valid_file(file))
        return bad_request(req, "bad album or file name");

    char dir[256], final_path[400], tmp_path[410];
    snprintf(dir, sizeof(dir), SD_MOUNT_POINT "/%s", album);
    snprintf(final_path, sizeof(final_path), "%s/%s", dir, file);
    snprintf(tmp_path, sizeof(tmp_path), "%s.part", final_path);
    mkdir(dir, 0777);

    FILE *f = fopen(tmp_path, "wb");
    char *buf = malloc(IO_BUF_SIZE);
    if (!f || !buf)
    {
        if (f)
            fclose(f);
        free(buf);
        return httpd_resp_send_500(req);
    }

    /* written to a .part file first so an aborted upload never leaves a broken track behind */
    size_t remaining = req->content_len;
    bool ok = true;
    int64_t t0 = esp_timer_get_time();
    while (remaining > 0)
    {
        int n = httpd_req_recv(req, buf, remaining < IO_BUF_SIZE ? remaining : IO_BUF_SIZE);
        if (n == HTTPD_SOCK_ERR_TIMEOUT)
            continue;
        if (n <= 0 || fwrite(buf, 1, n, f) != (size_t)n)
        {
            ok = false;
            break;
        }
        remaining -= n;
    }
    free(buf);
    ok = (fclose(f) == 0) && ok;

    if (!ok)
    {
        ESP_LOGW(TAG, "upload of %s failed (%u bytes missing)", file, (unsigned)remaining);
        remove(tmp_path);
        if (remaining == 0)     /* all received but not written: card full */
        {
            httpd_resp_set_status(req, "507 Insufficient Storage");
            return httpd_resp_sendstr(req, "write failed (card full?)");
        }
        return ESP_FAIL;
    }
    remove(final_path);
    if (rename(tmp_path, final_path) != 0)
        return httpd_resp_send_500(req);
    int64_t ms = (esp_timer_get_time() - t0) / 1000;
    ESP_LOGI(TAG, "stored %s (%u bytes, %lld ms, %u KB/s)", final_path, (unsigned)req->content_len, (long long)ms,
             (unsigned)(ms ? (int64_t)req->content_len / ms : 0));
    return send_ok(req);
}

static esp_err_t api_file_delete(httpd_req_t *req)
{
    char album[NAME_MAX_LEN + 4], file[NAME_MAX_LEN + 4], path[400];
    if (!get_param(req, "album", album, sizeof(album)) || !valid_name(album) ||
        !get_param(req, "file", file, sizeof(file)) || !valid_file(file))
        return bad_request(req, "bad album or file name");
    snprintf(path, sizeof(path), SD_MOUNT_POINT "/%s/%s", album, file);
    if (remove(path) != 0)
        return httpd_resp_send_404(req);
    return send_ok(req);
}

static esp_err_t api_album_delete(httpd_req_t *req)
{
    char album[NAME_MAX_LEN + 4], path[272], fp[560];
    if (!get_param(req, "album", album, sizeof(album)) || !valid_name(album))
        return bad_request(req, "bad album");
    snprintf(path, sizeof(path), SD_MOUNT_POINT "/%s", album);

    DIR *dir = opendir(path);
    if (!dir)
        return httpd_resp_send_404(req);
    struct dirent *e;
    while ((e = readdir(dir)) != NULL)
    {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        snprintf(fp, sizeof(fp), "%s/%s", path, e->d_name);
        remove(fp);
    }
    closedir(dir);
    if (rmdir(path) != 0)
        return httpd_resp_send_500(req);
    return send_ok(req);
}

static esp_err_t api_album_rename(httpd_req_t *req)
{
    char from[NAME_MAX_LEN + 4], to[NAME_MAX_LEN + 4], from_path[272], to_path[272], tmp_path[290];
    if (!get_param(req, "album", from, sizeof(from)) || !valid_name(from) ||
        !get_param(req, "to", to, sizeof(to)) || !valid_name(to))
        return bad_request(req, "bad album name");
    snprintf(from_path, sizeof(from_path), SD_MOUNT_POINT "/%s", from);
    snprintf(to_path, sizeof(to_path), SD_MOUNT_POINT "/%s", to);

    struct stat st;
    if (stat(from_path, &st) != 0)
        return httpd_resp_send_404(req);
    if (strcmp(from, to) == 0)
        return send_ok(req);

    if (strcasecmp(from, to) == 0)
    {
        /* FAT names are case-insensitive: a pure case change has to go through a temporary name */
        snprintf(tmp_path, sizeof(tmp_path), SD_MOUNT_POINT "/%s.rename", to);
        if (rename(from_path, tmp_path) != 0 || rename(tmp_path, to_path) != 0)
            return httpd_resp_send_500(req);
    }
    else
    {
        if (stat(to_path, &st) == 0)
        {
            httpd_resp_set_status(req, "409 Conflict");
            return httpd_resp_sendstr(req, "an album with that name already exists");
        }
        if (rename(from_path, to_path) != 0)
            return httpd_resp_send_500(req);
    }
    ESP_LOGI(TAG, "renamed album \"%s\" to \"%s\"", from, to);
    return send_ok(req);
}

/* ---------- firmware update ---------- */

static esp_err_t api_ota(httpd_req_t *req)
{
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no OTA partition");

    char *buf = malloc(IO_BUF_SIZE);
    if (!buf)
        return httpd_resp_send_500(req);

    esp_ota_handle_t handle = 0;
    size_t remaining = req->content_len;
    size_t filled = 0;
    bool started = false, ok = true;
    const char *err = NULL;
    int64_t t0 = esp_timer_get_time();

    while (remaining > 0 && ok)
    {
        int n = httpd_req_recv(req, buf + filled, (remaining < IO_BUF_SIZE - filled ? remaining : IO_BUF_SIZE - filled));
        if (n == HTTPD_SOCK_ERR_TIMEOUT)
            continue;
        if (n <= 0)
        {
            err = "receive failed";
            ok = false;
            break;
        }
        remaining -= n;
        filled += n;

        if (!started)
        {
            /* Reject anything that is not an app image of this project (e.g. the full image with bootloader):
             * the app descriptor sits right behind the image header and the first segment header. */
            size_t desc_end = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);
            if (filled < desc_end && remaining > 0)
                continue;
            const esp_app_desc_t *desc = (const esp_app_desc_t *)(buf + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
            if (filled < desc_end || buf[0] != (char)ESP_IMAGE_HEADER_MAGIC ||
                desc->magic_word != ESP_APP_DESC_MAGIC_WORD ||
                strcmp(desc->project_name, esp_app_get_description()->project_name) != 0)
            {
                err = "not an app image of this player (use the -ota.bin file)";
                ok = false;
                break;
            }
            if (esp_ota_begin(part, req->content_len, &handle) != ESP_OK)
            {
                err = "ota begin failed";
                ok = false;
                break;
            }
            started = true;
        }
        if (esp_ota_write(handle, buf, filled) != ESP_OK)
        {
            err = "flash write failed";
            ok = false;
            break;
        }
        filled = 0;
    }
    free(buf);

    if (ok && started && esp_ota_end(handle) == ESP_OK && esp_ota_set_boot_partition(part) == ESP_OK)
    {
        ESP_LOGI(TAG, "OTA ok (%u bytes, %lld ms), rebooting into %s", (unsigned)req->content_len,
                 (long long)((esp_timer_get_time() - t0) / 1000), part->label);
        send_ok(req);
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
        return ESP_OK;
    }
    if (started && !ok)
        esp_ota_abort(handle);
    ESP_LOGW(TAG, "OTA failed: %s", err ? err : "image invalid");
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, err ? err : "image invalid");
    return ESP_FAIL;
}

/* Battery state for the page: {"present":false} or voltage (smoothed, instantaneous), ADC input voltage and percent. */
static esp_err_t api_battery(httpd_req_t *req)
{
    char json[220];
    int percent;
    float volts, now;
    bool external;
    httpd_resp_set_type(req, "application/json");
    if (battery_read(&percent, &volts, &external) && battery_read_instant(&now))
        snprintf(json, sizeof(json),
                 "{\"present\":true,\"percent\":%d,\"volts\":%.3f,\"instant\":%.3f,\"adc\":%.3f,\"external\":%s,"
                 "\"charging\":%s,\"cal\":%.4f}",
                 percent, volts, now, now / 2.0f, external ? "true" : "false", battery_charging() ? "true" : "false",
                 settings_batt_cal() / 10000.0f);
    else
        snprintf(json, sizeof(json), "{\"present\":false}");
    return httpd_resp_sendstr(req, json);
}

/* POST /api/battery?volts=3.912  The battery voltage measured with a multimeter: the correction is set so that the
 * player shows this value. volts=0 resets the correction. Answers like GET /api/battery. */
static esp_err_t api_battery_calibrate(httpd_req_t *req)
{
    char v[16];
    int percent;
    float shown;
    bool external;
    if (!get_param(req, "volts", v, sizeof(v)))
        return bad_request(req, "volts is required");
    float measured = strtof(v, NULL);
    if (measured <= 0)
    {
        settings_set_batt_cal(SETTINGS_BATT_CAL_DEFAULT);
    }
    else
    {
        if (!battery_read(&percent, &shown, &external))
            return bad_request(req, "no battery voltage to calibrate");
        float cal = settings_batt_cal() * measured / shown;
        if (cal < SETTINGS_BATT_CAL_MIN || cal > SETTINGS_BATT_CAL_MAX)
            return bad_request(req, "the difference is too big for a calibration (more than 10 %)");
        settings_set_batt_cal((int)(cal + 0.5f));
    }
    return api_battery(req);
}

static esp_err_t api_settings_get(httpd_req_t *req)
{
    char json[160];
    snprintf(json, sizeof(json),
             "{\"dim_s\":%d,\"dim_level\":%d,\"dim_level_min\":%d,\"dim_level_max\":%d,\"dim_s_max\":%d}",
             settings_dim_timeout_s(), settings_dim_level(), SETTINGS_DIM_LEVEL_MIN, SETTINGS_DIM_LEVEL_MAX,
             SETTINGS_DIM_S_MAX);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

/* Appends s as a JSON string (quotes, backslashes and control characters escaped). */
static void json_string(char *out, size_t size, const char *s)
{
    size_t n = strlen(out);
    if (n + 1 < size)
        out[n++] = '"';
    for (; *s && n + 7 < size; s++)
    {
        if (*s == '"' || *s == '\\')
            out[n++] = '\\';
        if ((unsigned char)*s < 0x20)
            n += snprintf(out + n, size - n, "\\u%04x", *s);
        else
            out[n++] = *s;
    }
    if (n + 2 < size)
        out[n++] = '"';
    out[n] = 0;
}

/* GET /api/eq -> {"bass":3,"treble":2,"max":6} (dB) */
static esp_err_t api_eq_get(httpd_req_t *req)
{
    char json[80];
    snprintf(json, sizeof(json), "{\"bass\":%d,\"treble\":%d,\"max\":%d}", settings_eq_bass(), settings_eq_treble(),
             SETTINGS_EQ_MAX);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

/* POST /api/eq?bass=3&treble=2 (dB, clamped to 0..6); audible at once */
static esp_err_t api_eq_set(httpd_req_t *req)
{
    char a[8], b[8];
    if (!get_param(req, "bass", a, sizeof(a)) || !get_param(req, "treble", b, sizeof(b)))
        return bad_request(req, "bass and treble are required");
    if (settings_set_eq(atoi(a), atoi(b)) != ESP_OK)
        return httpd_resp_send_500(req);
    return api_eq_get(req);
}

/* GET /api/wifi ->{"ssid":"...","password":"..."}: the credentials the access point uses (the page needs them to edit) */
static esp_err_t api_wifi_get(httpd_req_t *req)
{
    char json[320] = "{\"ssid\":";
    json_string(json, sizeof(json), settings_wifi_ssid());
    strlcat(json, ",\"password\":", sizeof(json));
    json_string(json, sizeof(json), settings_wifi_password());
    strlcat(json, "}", sizeof(json));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

/* POST /api/wifi?ssid=...&password=...  Stored at once, used when the WiFi is switched on the next time (this connection
 * keeps working with the old one). */
static esp_err_t api_wifi_set(httpd_req_t *req)
{
    char ssid[SETTINGS_WIFI_SSID_MAX + 1], pass[SETTINGS_WIFI_PASS_MAX + 1];
    if (!get_param(req, "ssid", ssid, sizeof(ssid)) || !get_param(req, "password", pass, sizeof(pass)))
        return bad_request(req, "ssid and password are required (name up to 32, password 8 to 63 characters)");
    if (settings_set_wifi(ssid, pass) != ESP_OK)
        return bad_request(req, "name must be 1 to 32 characters, password 8 to 63 characters");
    return api_wifi_get(req);
}

/* POST /api/settings?dim_s=90&dim_level=190 (values are clamped to their ranges) */
static esp_err_t api_settings_set(httpd_req_t *req)
{
    char a[16], b[16];
    if (!get_param(req, "dim_s", a, sizeof(a)) || !get_param(req, "dim_level", b, sizeof(b)))
        return bad_request(req, "dim_s and dim_level are required");
    if (settings_set_dim(atoi(a), atoi(b)) != ESP_OK)
        return httpd_resp_send_500(req);
    return api_settings_get(req);
}

static esp_err_t api_done(httpd_req_t *req)
{
    if (s_on_done)
        s_on_done();
    return send_ok(req);
}

/* ---------- server ---------- */

esp_err_t web_server_start(void (*on_done)(void))
{
    if (s_server)
        return ESP_ERR_INVALID_STATE;
    s_on_done = on_done;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.core_id = 1;        // core 0 is busy with the display refill interrupt
    cfg.max_uri_handlers = 24;
    cfg.max_open_sockets = 5;
    cfg.recv_wait_timeout = 20;
    cfg.send_wait_timeout = 20;
    cfg.lru_purge_enable = true;
    esp_err_t ret = httpd_start(&s_server, &cfg);
    if (ret != ESP_OK)
        return ret;

    static const httpd_uri_t uris[] = {
        { .uri = "/",              .method = HTTP_GET,    .handler = page_index },
        { .uri = "/lame.min.js",   .method = HTTP_GET,    .handler = page_lame },
        { .uri = "/api/albums",    .method = HTTP_GET,    .handler = api_albums },
        { .uri = "/api/cover",     .method = HTTP_GET,    .handler = api_cover },
        { .uri = "/api/file",      .method = HTTP_PUT,    .handler = api_file_put },
        { .uri = "/api/file",      .method = HTTP_DELETE, .handler = api_file_delete },
        { .uri = "/api/album",     .method = HTTP_DELETE, .handler = api_album_delete },
        { .uri = "/api/rename",    .method = HTTP_POST,   .handler = api_album_rename },
        { .uri = "/api/ota",       .method = HTTP_POST,   .handler = api_ota },
        { .uri = "/api/battery",   .method = HTTP_GET,    .handler = api_battery },
        { .uri = "/api/battery",   .method = HTTP_POST,   .handler = api_battery_calibrate },
        { .uri = "/screenshot.png", .method = HTTP_GET,   .handler = screenshot_send_png },
        { .uri = "/api/eq",        .method = HTTP_GET,    .handler = api_eq_get },
        { .uri = "/api/eq",        .method = HTTP_POST,   .handler = api_eq_set },
        { .uri = "/api/wifi",      .method = HTTP_GET,   .handler = api_wifi_get },
        { .uri = "/api/wifi",      .method = HTTP_POST,   .handler = api_wifi_set },
        { .uri = "/api/settings",  .method = HTTP_GET,  .handler = api_settings_get },
        { .uri = "/api/settings",  .method = HTTP_POST,   .handler = api_settings_set },
        { .uri = "/api/done",      .method = HTTP_POST,   .handler = api_done },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++)
        httpd_register_uri_handler(s_server, &uris[i]);
    return ESP_OK;
}

void web_server_stop(void)
{
    if (s_server)
    {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
