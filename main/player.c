#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mp3dec.h"

#include "eq.h"
#include "player.h"
#include "sunton_esp32_8048s050c.h"

static const char *TAG = "player";

#define IN_BUF_SIZE     8192
#define REFILL_BELOW    (2 * MAINBUF_SIZE)
#define PCM_SAMPLES     (1152 * 2)      /* one MPEG-1 layer 3 frame, stereo */
#define DEFAULT_RATE    44100
#define PLAYER_DOWNMIX_TO_MONO  1   /* 1: stereo -> mono on both channels (one speaker), 0: keep stereo */
#define DEFAULT_VOLUME  5

typedef enum { CMD_PLAY_ALBUM, CMD_TOGGLE, CMD_STOP, CMD_NEXT, CMD_PREV, CMD_VOL_UP, CMD_VOL_DOWN, CMD_RESET, CMD_SHUFFLE, CMD_LIBRARY, CMD_VOLUME } cmd_id_t;

typedef struct {
    cmd_id_t id;
    const library_album_t *album;
    size_t track;
} cmd_t;

/* Everything below except the queue is owned by the player task. */
static QueueHandle_t s_queue;
static SemaphoreHandle_t s_reset_sem;
static player_event_cb_t s_cb;
static i2s_chan_handle_t s_tx;
static bool s_i2s_on;
static bool s_i2s_stoppable;    /* true only while the sample rate is being changed */
static uint32_t s_rate = DEFAULT_RATE;

static const library_album_t *s_album;
static size_t s_track;          /* position in the play order (the track number when not shuffling) */
static const library_album_t *s_albums;     /* the whole library, handed over by the UI */
static size_t s_n_albums;
static bool s_shuffle;          /* shuffle mode: play all tracks of all albums in random order */
static uint32_t *s_entries;     /* shuffle play order: (album index << 16) | track index */
static size_t s_n_entries;
static volatile int s_cur_album = -1;       /* album of the current track, read by the UI */
static player_state_t s_state = PLAYER_STOPPED;
static int s_volume = DEFAULT_VOLUME;

static uint8_t s_in[IN_BUF_SIZE];
static short s_pcm[PCM_SAMPLES];

static const char *track_path(size_t pos)
{
    if (s_shuffle)
    {
        uint32_t e = s_entries[pos];
        return s_albums[e >> 16].tracks[e & 0xffff];
    }
    return s_album->tracks[pos];
}

static size_t play_count(void)
{
    return s_shuffle ? s_n_entries : s_album->n_tracks;
}

/* Album index and track number (inside that album) of the current play position. */
static void current_pos(int *album, size_t *track)
{
    if (s_shuffle && s_entries && s_track < s_n_entries)
    {
        *album = (int)(s_entries[s_track] >> 16);
        *track = s_entries[s_track] & 0xffff;
        return;
    }
    *track = s_track;
    *album = (s_album && s_albums && s_album >= s_albums && s_album < s_albums + s_n_albums) ? (int)(s_album - s_albums) : -1;
}

/* New random order of every track of every album. (first_album, first_track), if first_album >= 0, plays first. */
static bool shuffle_all(int first_album, size_t first_track)
{
    size_t total = 0;
    for (size_t a = 0; a < s_n_albums; a++)
        total += s_albums[a].n_tracks;
    if (!total)
        return false;
    uint32_t *e = realloc(s_entries, total * sizeof(uint32_t));
    if (!e)
        return false;
    s_entries = e;
    s_n_entries = total;
    size_t k = 0;
    for (size_t a = 0; a < s_n_albums; a++)
        for (size_t t = 0; t < s_albums[a].n_tracks; t++)
            e[k++] = ((uint32_t)a << 16) | (uint32_t)t;
    for (size_t i = total; i > 1; i--)
    {
        size_t j = esp_random() % i;
        uint32_t tmp = e[i - 1];
        e[i - 1] = e[j];
        e[j] = tmp;
    }
    if (first_album >= 0)
    {
        uint32_t want = ((uint32_t)first_album << 16) | (uint32_t)first_track;
        for (size_t i = 0; i < total; i++)
            if (e[i] == want)
            {
                e[i] = e[0];
                e[0] = want;
                break;
            }
    }
    return true;
}

static void notify(void)
{
    int album;
    size_t track;
    current_pos(&album, &track);
    s_cur_album = album;
    if (s_cb)
        s_cb(s_state, track, s_volume);
}

static void i2s_set_on(bool on)
{
    /* The I2S output stays enabled once started (auto_clear sends silence while nothing is written): switching the
     * clocks and the amplifier on and off at every play/pause/stop was a suspect for glitches on the display.
     * Only the sample-rate change below really stops the channel. */
    if (on == s_i2s_on || (!on && !s_i2s_stoppable))
        return;
    if (on)
        ESP_ERROR_CHECK(i2s_channel_enable(s_tx));
    else
        ESP_ERROR_CHECK(i2s_channel_disable(s_tx));
    s_i2s_on = on;
}

static void i2s_set_rate(uint32_t rate)
{
    if (rate == s_rate)
        return;
    bool was_on = s_i2s_on;
    s_i2s_stoppable = true;
    i2s_set_on(false);
    s_i2s_stoppable = false;
    i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(rate);
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_clock(s_tx, &clk));
    s_rate = rate;
    i2s_set_on(was_on);
}

/* Perceptual volume: gain = floor + (1 - floor) * (level / max)^2 in Q15. The floor keeps the lowest setting
 * quietly audible instead of muted (PLAYER_MIN_GAIN_PERMILLE 12 = 1.2 % = -38 dB). */
#define PLAYER_MIN_GAIN_PERMILLE    12

static int32_t volume_gain(void)
{
    int64_t one = 32768;
    int64_t floor = one * PLAYER_MIN_GAIN_PERMILLE / 1000;
    return (int32_t)(floor + (one - floor) * s_volume * s_volume / (PLAYER_VOLUME_MAX * PLAYER_VOLUME_MAX));
}

/* Applies a command. Returns true when the track being decoded must be abandoned. */
static bool handle_cmd(const cmd_t *c)
{
    switch (c->id)
    {
    case CMD_PLAY_ALBUM:
        s_album = c->album;
        s_track = c->track;
        if (s_shuffle)      /* shuffle mode: whatever album was asked for, start with a random track of all */
        {
            if (shuffle_all(-1, 0))
                s_track = 0;
            else
                s_shuffle = false;
        }
        s_state = PLAYER_PLAYING;
        notify();
        return true;
    case CMD_TOGGLE:
        if (s_state == PLAYER_PLAYING)
        {
            s_state = PLAYER_PAUSED;
            i2s_set_on(false);
            notify();
        }
        else if (s_state == PLAYER_PAUSED)
        {
            s_state = PLAYER_PLAYING;
            i2s_set_on(true);
            notify();
        }
        else if (s_album)
        {
            s_state = PLAYER_PLAYING;
            notify();
            return true;
        }
        return false;
    case CMD_STOP:
        if (s_state == PLAYER_STOPPED)
            return false;
        s_state = PLAYER_STOPPED;
        i2s_set_on(false);
        notify();
        return true;
    case CMD_NEXT:
    case CMD_PREV:
        if (!s_album)
            return false;
        {
            size_t n = play_count();
            if (!n)
                return false;
            s_track = c->id == CMD_NEXT ? (s_track + 1) % n : (s_track + n - 1) % n;
        }
        s_state = PLAYER_PLAYING;
        notify();
        return true;
    case CMD_RESET:
        s_album = NULL;
        s_albums = NULL;        /* the library is rescanned, the UI hands the new one over */
        s_n_albums = 0;
        s_track = 0;
        s_state = PLAYER_STOPPED;
        notify();
        xSemaphoreGive(s_reset_sem);
        return true;
    case CMD_SHUFFLE:
    {
        bool on = c->track != 0;
        if (on == s_shuffle)
            return false;
        int album;
        size_t track;
        if (!s_album)
        {
            s_shuffle = on;         /* nothing loaded yet: the next album start uses it */
            return false;
        }
        current_pos(&album, &track);
        if (on)
        {
            if (album >= 0 && shuffle_all(album, track))   /* the track that plays now stays first */
            {
                s_shuffle = true;
                s_track = 0;
            }
        }
        else
        {
            if (album >= 0)
            {
                s_album = &s_albums[album];     /* back to the album order, continue after this track */
                s_track = track;
            }
            s_shuffle = false;
        }
        notify();
        return false;
    }
    case CMD_VOLUME:
        s_volume = c->track > PLAYER_VOLUME_MAX ? PLAYER_VOLUME_MAX : (int)c->track;
        notify();
        return false;
    case CMD_LIBRARY:
        s_albums = c->album;
        s_n_albums = c->track;
        return false;
    case CMD_VOL_UP:
        if (s_volume < PLAYER_VOLUME_MAX)
            s_volume++;
        notify();
        return false;
    case CMD_VOL_DOWN:
        if (s_volume > 0)
            s_volume--;
        notify();
        return false;
    }
    return false;
}

/* Skips an ID3v2 tag at the current position (embedded cover art can be large). */
static void skip_id3v2(FILE *f)
{
    uint8_t h[10];
    if (fread(h, 1, sizeof(h), f) == sizeof(h) && memcmp(h, "ID3", 3) == 0)
    {
        long size = ((h[6] & 0x7f) << 21) | ((h[7] & 0x7f) << 14) | ((h[8] & 0x7f) << 7) | (h[9] & 0x7f);
        fseek(f, size, SEEK_CUR);
    }
    else
    {
        fseek(f, 0, SEEK_SET);
    }
}

static void write_pcm(const MP3FrameInfo *info)
{
    int frames = info->outputSamps / info->nChans;
    int32_t gain = volume_gain();

    if (info->nChans == 1)
    {
        /* expand mono to stereo in place, from the back */
        for (int i = frames - 1; i >= 0; i--)
            s_pcm[2 * i] = s_pcm[2 * i + 1] = s_pcm[i];
    }
#if PLAYER_DOWNMIX_TO_MONO
    else
    {
        /* only one speaker channel is connected: put (L+R)/2 on both, so nothing of a stereo recording is lost */
        for (int i = 0; i < frames; i++)
            s_pcm[2 * i] = s_pcm[2 * i + 1] = (short)(((int32_t)s_pcm[2 * i] + s_pcm[2 * i + 1]) >> 1);
    }
#endif
    /* tone control, volume and limiter; both channels carry the same signal when mono (expanded or downmixed) */
    eq_process(s_pcm, frames, s_rate, info->nChans == 1 || PLAYER_DOWNMIX_TO_MONO, gain);

    size_t written;
    i2s_channel_write(s_tx, s_pcm, frames * 2 * sizeof(short), &written, portMAX_DELAY);
}

/* Decodes and plays one file. Returns true if it played to the end, false if a command ended it early. */
static bool play_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        ESP_LOGE(TAG, "cannot open %s", path);
        return true;    /* skip to the next track */
    }
    HMP3Decoder dec = MP3InitDecoder();
    if (!dec)
    {
        fclose(f);
        ESP_LOGE(TAG, "decoder alloc failed");
        s_state = PLAYER_STOPPED;
        notify();
        return false;
    }
    ESP_LOGI(TAG, "playing %s", path);

    skip_id3v2(f);
    i2s_set_on(true);

    int filled = 0, pos = 0;
    bool eof = false, completed = true;
    MP3FrameInfo info;
    cmd_t cmd;

    for (;;)
    {
        /* commands: polled between frames; while paused wait here */
        while (xQueueReceive(s_queue, &cmd, s_state == PLAYER_PAUSED ? portMAX_DELAY : 0) == pdTRUE)
        {
            if (handle_cmd(&cmd))
            {
                completed = false;
                goto done;
            }
            if (s_state != PLAYER_PAUSED)
                break;
        }

        if (!eof && filled - pos < REFILL_BELOW)
        {
            memmove(s_in, s_in + pos, filled - pos);
            filled -= pos;
            pos = 0;
            size_t n = fread(s_in + filled, 1, IN_BUF_SIZE - filled, f);
            if (n == 0)
                eof = true;
            filled += n;
        }

        int off = MP3FindSyncWord(s_in + pos, filled - pos);
        if (off < 0)
        {
            if (eof)
                break;
            pos = filled;
            continue;
        }
        int start = pos + off;
        uint8_t *p = s_in + start;
        int left = filled - start;
        int err = MP3Decode(dec, &p, &left, s_pcm, 0);
        pos = filled - left;

        if (err == ERR_MP3_NONE)
        {
            MP3GetLastFrameInfo(dec, &info);
            if (info.samprate > 0 && (info.nChans == 1 || info.nChans == 2) && info.bitsPerSample == 16)
            {
                i2s_set_rate(info.samprate);
                write_pcm(&info);
            }
        }
        else if (err == ERR_MP3_INDATA_UNDERFLOW)
        {
            if (eof)
                break;
            if (filled - start >= REFILL_BELOW)
                pos = start + 1;    /* no progress possible, resync */
        }
        else if (err != ERR_MP3_MAINDATA_UNDERFLOW)
        {
            pos = start + 1;        /* corrupt frame, resync */
        }
    }

done:
    MP3FreeDecoder(dec);
    fclose(f);
    return completed;
}

static void player_task(void *arg)
{
    cmd_t cmd;
    for (;;)
    {
        if (s_state != PLAYER_PLAYING)
        {
            xQueueReceive(s_queue, &cmd, portMAX_DELAY);
            handle_cmd(&cmd);
            continue;
        }
        if (play_file(track_path(s_track)) && s_state == PLAYER_PLAYING)
        {
            if (s_track + 1 < play_count())
            {
                s_track++;
            }
            else if (s_shuffle && shuffle_all(-1, 0))
            {
                s_track = 0;        /* played everything: shuffle again, endless */
            }
            else
            {
                /* end of album */
                s_state = PLAYER_STOPPED;
                s_track = 0;
                i2s_set_on(false);
            }
            notify();
        }
    }
}

esp_err_t player_init(player_event_cb_t cb)
{
    s_cb = cb;
    s_queue = xQueueCreate(8, sizeof(cmd_t));
    s_reset_sem = xSemaphoreCreateBinary();
    if (!s_queue || !s_reset_sem)
        return ESP_ERR_NO_MEM;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;     /* silence instead of repeating the last buffer on underrun */
    chan_cfg.dma_desc_num = 8;      /* ~85 ms of buffer so a busy LVGL (cover decoding) cannot starve the audio */
    chan_cfg.dma_frame_num = 480;
    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_tx, NULL);
    if (ret != ESP_OK)
        return ret;

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(DEFAULT_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = SUNTON_ESP32_I2S_PIN_BCLK,
            .ws = SUNTON_ESP32_I2S_PIN_LRC,
            .dout = SUNTON_ESP32_I2S_PIN_DOUT,
            .din = I2S_GPIO_UNUSED,
        },
    };
    ret = i2s_channel_init_std_mode(s_tx, &std_cfg);
    if (ret != ESP_OK)
        return ret;
    i2s_set_on(true);       /* stays running, sends silence until the first track (a disabled I2S hisses) */

    /* above LVGL (3) so audio never starves; core 1, WiFi will live on core 0 */
    if (xTaskCreatePinnedToCore(player_task, "player", 8192, NULL, 5, NULL, 1) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}

static void send(cmd_id_t id, const library_album_t *album, size_t track)
{
    cmd_t c = { .id = id, .album = album, .track = track };
    xQueueSend(s_queue, &c, 0);
}

void player_play_album(const library_album_t *album, size_t track)
{
    if (album && track < album->n_tracks)
        send(CMD_PLAY_ALBUM, album, track);
}
void player_set_volume(int level) { send(CMD_VOLUME, NULL, level < 0 ? 0 : (size_t)level); }
void player_set_shuffle(bool on) { send(CMD_SHUFFLE, NULL, on ? 1 : 0); }
void player_set_library(const library_album_t *albums, size_t n) { send(CMD_LIBRARY, albums, n); }
int player_album_index(void) { return s_cur_album; }
void player_toggle_pause(void) { send(CMD_TOGGLE, NULL, 0); }
void player_stop(void)         { send(CMD_STOP, NULL, 0); }
void player_next(void)         { send(CMD_NEXT, NULL, 0); }
void player_prev(void)         { send(CMD_PREV, NULL, 0); }
void player_volume_up(void)    { send(CMD_VOL_UP, NULL, 0); }
void player_volume_down(void)  { send(CMD_VOL_DOWN, NULL, 0); }

void player_reset(void)
{
    cmd_t c = { .id = CMD_RESET };
    xSemaphoreTake(s_reset_sem, 0);
    if (xQueueSend(s_queue, &c, pdMS_TO_TICKS(100)) == pdTRUE)
        xSemaphoreTake(s_reset_sem, pdMS_TO_TICKS(2000));
}

int player_volume(void)
{
    return s_volume;
}
