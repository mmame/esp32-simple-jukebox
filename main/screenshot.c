#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"
#include "miniz.h"

#include "screenshot.h"

static const char *TAG = "screenshot";

#define IDAT_MAX    4096        /* compressed bytes per PNG IDAT chunk */

typedef struct {
    httpd_req_t *req;
    uint8_t *out;               /* 8 bytes chunk header, IDAT_MAX data, 4 bytes crc */
    size_t used;                /* data bytes in out */
    bool failed;
} sink_t;

static uint32_t s_crc_table[256];

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n)
{
    if (!s_crc_table[1])
    {
        for (uint32_t i = 0; i < 256; i++)
        {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            s_crc_table[i] = c;
        }
    }
    crc = ~crc;
    while (n--)
        crc = s_crc_table[(crc ^ *p++) & 0xff] ^ (crc >> 8);
    return ~crc;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

/* Sends one PNG chunk: length, type, data (already at out + 8), crc. */
static void send_chunk(sink_t *s, const char type[4], size_t len)
{
    if (s->failed)
        return;
    put_be32(s->out, (uint32_t)len);
    memcpy(s->out + 4, type, 4);
    put_be32(s->out + 8 + len, crc32_update(0, s->out + 4, 4 + len));
    if (httpd_resp_send_chunk(s->req, (const char *)s->out, 12 + len) != ESP_OK)
        s->failed = true;
}

/* Called by the compressor with output; collects it into IDAT chunks. */
static mz_bool put_buf(const void *buf, int len, void *user)
{
    sink_t *s = user;
    const uint8_t *p = buf;
    while (len > 0)
    {
        size_t n = IDAT_MAX - s->used;
        if (n > (size_t)len)
            n = len;
        memcpy(s->out + 8 + s->used, p, n);
        s->used += n;
        p += n;
        len -= n;
        if (s->used == IDAT_MAX)
        {
            send_chunk(s, "IDAT", s->used);
            s->used = 0;
        }
    }
    return !s->failed;
}

static void *big_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
}

esp_err_t screenshot_send_png(httpd_req_t *req)
{
    lv_lock();
    lv_draw_buf_t *snap = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    lv_unlock();
    if (!snap)
    {
        ESP_LOGW(TAG, "snapshot failed (not enough memory?)");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "not enough memory for a screenshot");
        return ESP_FAIL;
    }

    const uint32_t w = snap->header.w, h = snap->header.h, stride = snap->header.stride;
    tdefl_compressor *comp = big_alloc(sizeof(tdefl_compressor));
    uint8_t *row = malloc(1 + 3 * w);
    sink_t sink = { .req = req, .out = malloc(IDAT_MAX + 12) };
    esp_err_t ret = ESP_FAIL;

    if (comp && row && sink.out)
    {
        httpd_resp_set_type(req, "image/png");
        httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=\"screenshot.png\"");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");

        static const uint8_t signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
        httpd_resp_send_chunk(req, (const char *)signature, sizeof(signature));
        put_be32(sink.out + 8, w);          /* IHDR: size, 8 bit, truecolor, no interlace */
        put_be32(sink.out + 12, h);
        sink.out[16] = 8;
        sink.out[17] = 2;
        sink.out[18] = sink.out[19] = sink.out[20] = 0;
        send_chunk(&sink, "IHDR", 13);

        /* fast settings: greedy parsing and few probes, the screen has large flat areas anyway */
        tdefl_init(comp, put_buf, &sink, TDEFL_WRITE_ZLIB_HEADER | TDEFL_GREEDY_PARSING_FLAG | 16);
        for (uint32_t y = 0; y < h && !sink.failed; y++)
        {
            const uint16_t *src = (const uint16_t *)(snap->data + y * stride);
            uint8_t *o = row;
            *o++ = 0;                       /* filter type: none */
            for (uint32_t x = 0; x < w; x++)
            {
                const uint16_t p = src[x];
                const uint8_t r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
                *o++ = (r << 3) | (r >> 2);
                *o++ = (g << 2) | (g >> 4);
                *o++ = (b << 3) | (b >> 2);
            }
            tdefl_compress_buffer(comp, row, 1 + 3 * w, TDEFL_NO_FLUSH);
        }
        tdefl_compress_buffer(comp, NULL, 0, TDEFL_FINISH);
        if (sink.used)
            send_chunk(&sink, "IDAT", sink.used);
        send_chunk(&sink, "IEND", 0);
        if (!sink.failed)
            ret = httpd_resp_send_chunk(req, NULL, 0);
    }
    else
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "not enough memory for a screenshot");
    }

    free(comp);
    free(row);
    free(sink.out);
    lv_lock();
    lv_draw_buf_destroy(snap);
    lv_unlock();
    return ret;
}
