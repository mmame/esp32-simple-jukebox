#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "src/misc/cache/instance/lv_image_header_cache.h"

#include "ui.h"
#include "display_board.h"
#include "sunton_esp32_8048s050c.h"
#include "upload_mode.h"
#include "sd_card.h"

#define BAR_H           120
#define BTN_H           100
#define COVER           360     /* = screen height minus the control bar: the cover also uses the top area */
#define GRID_PAD        32
#define ARROW_W         160
#define ARROW_H         210
#define ARROW_MARGIN    16
#define POLL_MS         100
#define SD_RETRY_MS     2000    /* no card mounted: look again this often */
#define AUTOSTART_MS    3000    /* browsing albums stops the music; this long after the last arrow tap the shown album starts */
#define SCROLL_LIMIT    24      /* px a finger may move before a tap becomes a scroll (shaky hands) */
#define DONE_DELAY_MS   600     /* let the browser receive the reply of /api/done before WiFi goes down */

#define COL_BG          0x14181f
#define COL_CARD        0x2a313c
#define COL_ACCENT      0xffb300
#define COL_BLUE        0x3b82f6
#define COL_GREEN       0x22c55e
#define COL_RED         0xef4444

typedef enum { ACT_PREV, ACT_TOGGLE, ACT_NEXT, ACT_STOP, ACT_VOL_DOWN, ACT_VOL_UP, ACT_WIFI, ACT_UPLOAD_DONE,
               ACT_ALBUM_PREV, ACT_ALBUM_NEXT, ACT_SHUFFLE } action_t;

static const library_album_t *s_albums;
static size_t s_n_albums;
static lv_obj_t *s_content;     /* album page (cover + arrows), or the "no music" message */
static lv_obj_t *s_card;        /* the cover of the album shown */
static lv_obj_t *s_badge;
static lv_obj_t *s_title;       /* album title, or the song name while the album plays */
static int s_shown;             /* album shown on screen (browsing); s_current is the one that plays */
static bool s_autostart;        /* browsing stopped the music: start the shown album once the finger rests */
static uint32_t s_browse_tick;  /* time of the last arrow tap */
static bool s_dirty;            /* shown album changed: poll() must refresh the card */
static lv_obj_t *s_play_label;
static lv_obj_t *s_vol_bar;
static lv_obj_t *s_shuffle_btn;
static lv_obj_t *s_arrows[2];       /* previous / next album; hidden in shuffle mode (the screen follows the music) */
static bool s_shuffle_on;
static lv_obj_t *s_upload_box;      /* modal message box while upload mode is on */
static int s_current = -1;      /* album that was last started (LVGL task only) */

/* Written by the player task, read by poll(). */
static volatile int s_ev_state = PLAYER_STOPPED;
static volatile int s_ev_track;
static volatile int s_ev_volume = -1;
/* Set by the HTTP task when the upload page is finished. */
static volatile bool s_upload_done;

void ui_player_event(player_state_t state, size_t track, int volume)
{
    s_ev_track = (int)track;
    s_ev_volume = volume;
    s_ev_state = state;
}

static void on_upload_done_request(void)
{
    s_upload_done = true;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    return l;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, int width, int height, uint32_t color, action_t act,
                             const lv_font_t *font);
static void on_button(lv_event_t *e);

/* ---------- upload mode ---------- */

static void upload_box_create(const upload_info_t *info, esp_err_t err)
{
    lv_obj_t *box = lv_msgbox_create(NULL);     /* no parent: modal, dims the screen behind it */
    s_upload_box = box;
    lv_obj_set_size(box, 740, 400);
    lv_obj_set_style_text_font(box, &lv_font_montserrat_28, 0);
    lv_msgbox_add_title(box, "Add music with the web page");

    lv_obj_t *content = lv_msgbox_get_content(box);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(content, 24, 0);

    if (err == ESP_OK)
    {
        char wifi[96];
        snprintf(wifi, sizeof(wifi), "WIFI:T:WPA;S:%s;P:%s;;", info->ssid, info->password);
        lv_obj_t *qr_bg = lv_obj_create(content);
        lv_obj_set_size(qr_bg, 220, 220);
        lv_obj_set_style_bg_color(qr_bg, lv_color_white(), 0);
        lv_obj_set_style_pad_all(qr_bg, 8, 0);
        lv_obj_remove_flag(qr_bg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *qr = lv_qrcode_create(qr_bg);
        lv_qrcode_set_size(qr, 204);
        lv_qrcode_set_dark_color(qr, lv_color_black());
        lv_qrcode_set_light_color(qr, lv_color_white());
        lv_qrcode_update(qr, wifi, strlen(wifi));
        lv_obj_center(qr);

        lv_obj_t *text = lv_label_create(content);
        lv_label_set_text_fmt(text,
                              "WiFi:\n  %s\n"
                              "Password:\n  %s\n"
                              "Web page:\n  %s",
                              info->ssid, info->password, info->url);
        lv_obj_set_flex_grow(text, 1);
    }
    else
    {
        lv_obj_t *text = lv_label_create(content);
        lv_label_set_text(text, "Could not start WiFi.\nIs an SD card inserted?");
    }

    lv_obj_t *done = lv_msgbox_add_footer_button(box, LV_SYMBOL_OK " Done (WiFi off)");
    lv_obj_set_height(done, 80);
    lv_obj_set_style_bg_color(done, lv_color_hex(COL_GREEN), 0);
    lv_obj_set_style_text_font(done, &lv_font_montserrat_28, 0);
    lv_obj_add_event_cb(done, on_button, LV_EVENT_CLICKED, (void *)(intptr_t)ACT_UPLOAD_DONE);
    lv_obj_center(box);
}

static void build_library(void);

static void upload_enter(void)
{
    if (s_upload_box)
        return;
    upload_info_t info;
    s_upload_done = false;
    s_autostart = false;
    s_current = -1;
    board_lcd_set_low_bandwidth(true);      /* before WiFi starts, see sunton_esp32_8048s050c.c */
    esp_err_t err = upload_mode_start(&info, on_upload_done_request);
    upload_box_create(&info, err);
}

static void upload_leave(void)
{
    lv_msgbox_close_async(s_upload_box);
    s_upload_box = NULL;
    upload_mode_stop();
    board_lcd_set_low_bandwidth(false);

    /* the library changed: rescan and rebuild the album page (player_reset() already ran in upload_mode_start) */
    library_scan();
    s_current = -1;
    lv_image_cache_drop(NULL);
    lv_image_header_cache_drop(NULL);
    size_t n = 0;
    s_albums = library_albums(&n);
    s_n_albums = n;
    build_library();
}

/* ---------- album page ---------- */

static void on_cover(lv_event_t *e)
{
    (void)e;
    s_autostart = false;
    if (s_current == s_shown && s_ev_state != PLAYER_STOPPED)
    {
        player_toggle_pause();      /* the album that plays: pause / resume */
        return;
    }
    s_current = s_shown;
    player_play_album(&s_albums[s_shown], 0);
}

static lv_obj_t *make_card(lv_obj_t *parent, size_t idx);

static void show_album(int idx)
{
    s_shown = idx;
    if (s_card)
        lv_obj_delete(s_card);
    s_card = make_card(s_content, (size_t)idx);
    lv_obj_align(s_card, LV_ALIGN_TOP_MID, 0, 0);
    s_dirty = true;
}

/* "/sdcard/Album/03_Some Song.mp3" -> "Some Song" */
static void song_title(const char *path, char *out, size_t n)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    const char *p = base;
    while (*p >= '0' && *p <= '9')
        p++;
    if (p > base && *p == '_')
        base = p + 1;
    snprintf(out, n, "%s", base);
    char *dot = strrchr(out, '.');
    if (dot)
        *dot = 0;
}

/* amber while shuffle is on, grey otherwise */
static void update_shuffle_button(void)
{
    lv_obj_set_style_bg_color(s_shuffle_btn, lv_color_hex(s_shuffle_on ? COL_ACCENT : COL_CARD), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(s_shuffle_btn, 0), s_shuffle_on ? lv_color_black() : lv_color_white(), 0);
    for (int i = 0; i < 2; i++)
    {
        if (!s_arrows[i])
            continue;
        if (s_shuffle_on)
            lv_obj_add_flag(s_arrows[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_remove_flag(s_arrows[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/* Nothing playing: play/prev/next start the album on screen instead. Returns true if it did. */
static bool start_shown_if_stopped(void)
{
    if ((s_ev_state != PLAYER_STOPPED && !s_autostart) || !s_n_albums)
        return false;
    s_autostart = false;
    s_current = s_shown;
    player_play_album(&s_albums[s_shown], 0);
    return true;
}

/* Arrow tap: stop the music, and (if something was playing) start the newly shown album when browsing pauses. */
static void browse(int idx)
{
    if (s_ev_state != PLAYER_STOPPED)
    {
        player_stop();
        s_current = -1;
        s_autostart = true;
    }
    s_browse_tick = lv_tick_get();
    show_album(idx);
}

static void on_button(lv_event_t *e)
{
    switch ((action_t)(intptr_t)lv_event_get_user_data(e))
    {
    case ACT_PREV:          if (!start_shown_if_stopped()) player_prev();          break;
    case ACT_TOGGLE:        if (!start_shown_if_stopped()) player_toggle_pause();  break;
    case ACT_NEXT:          if (!start_shown_if_stopped()) player_next();          break;
    case ACT_STOP:          s_autostart = false; s_current = -1; player_stop(); break;
    case ACT_VOL_DOWN:      player_volume_down();   break;
    case ACT_VOL_UP:        player_volume_up();     break;
    case ACT_WIFI:          upload_enter();         break;
    case ACT_UPLOAD_DONE:   upload_leave();         break;
    case ACT_SHUFFLE:
        s_shuffle_on = !s_shuffle_on;
        player_set_shuffle(s_shuffle_on);
        update_shuffle_button();
        if (s_shuffle_on && s_ev_state == PLAYER_STOPPED)       /* nothing plays: shuffle starts the music */
            start_shown_if_stopped();
        break;
    case ACT_ALBUM_PREV:    browse((s_shown + (int)s_n_albums - 1) % (int)s_n_albums); break;
    case ACT_ALBUM_NEXT:    browse((s_shown + 1) % (int)s_n_albums);                   break;
    }
}

static lv_obj_t *make_card(lv_obj_t *parent, size_t idx)
{
    const library_album_t *a = &s_albums[idx];

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, COVER, COVER);
    lv_obj_set_style_pad_all(card, 0, 0);
    /* Square, unclipped cards on purpose: rounded clipping renders the cover through a 360x360 ARGB layer in PSRAM,
     * and that burst of memory traffic made the picture shift sideways whenever a track started or stopped.
     * The selection frame is drawn on top (border_post) so it does not shrink the content either. */
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_border_post(card, true, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(card, on_cover, LV_EVENT_CLICKED, NULL);

    /* children stay non-clickable so every tap reaches the card */
    if (a->cover)
    {
        char path[256];
        snprintf(path, sizeof(path), "S:%s", a->cover);
        lv_obj_t *img = lv_image_create(card);
        lv_image_set_src(img, path);
        lv_image_set_inner_align(img, LV_IMAGE_ALIGN_STRETCH);
        lv_obj_set_size(img, COVER, COVER);
        lv_obj_align(img, LV_ALIGN_TOP_LEFT, 0, 0);
    }
    else
    {
        lv_obj_t *icon = make_label(card, LV_SYMBOL_AUDIO, &lv_font_montserrat_36);
        lv_obj_center(icon);
    }

    lv_obj_t *title = make_label(card, a->title, &lv_font_montserrat_28);
    s_title = title;
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(title, COVER);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(title, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(title, LV_OPA_60, 0);
    lv_obj_set_style_pad_all(title, 8, 0);
    lv_obj_align(title, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t *badge = make_label(card, "", &lv_font_montserrat_28);
    lv_obj_set_style_bg_color(badge, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(badge, lv_color_black(), 0);
    lv_obj_set_style_pad_hor(badge, 12, 0);
    lv_obj_set_style_pad_ver(badge, 6, 0);
    lv_obj_set_style_radius(badge, 12, 0);
    lv_obj_align(badge, LV_ALIGN_TOP_LEFT, 8, 8);
    lv_obj_add_flag(badge, LV_OBJ_FLAG_HIDDEN);

    s_badge = badge;
    return card;
}

/* (Re)creates the area above the control bar: one cover in the middle, a big arrow on each side to change album. */
static void build_library(void)
{
    if (s_content)
        lv_obj_delete(s_content);
    s_card = s_badge = NULL;
    s_arrows[0] = s_arrows[1] = NULL;
    player_set_library(s_albums, s_n_albums);       /* for the shuffle over all albums */

    lv_obj_t *scr = lv_screen_active();
    s_content = lv_obj_create(scr);
    lv_obj_set_size(s_content, LV_PCT(100), SUNTON_ESP32_LCD_HEIGHT - BAR_H);
    lv_obj_align(s_content, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(s_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_content, 0, 0);
    lv_obj_set_style_radius(s_content, 0, 0);
    lv_obj_set_style_pad_all(s_content, 0, 0);
    lv_obj_remove_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_to_index(s_content, 0);

    if (s_n_albums == 0)
    {
        lv_obj_t *msg = make_label(s_content,
                                   sd_card_is_mounted() ? "No music yet.\n\nTap the WiFi symbol (top right) to add music."
                                                        : "No SD card found.\n\nPlease insert the card.",
                                   &lv_font_montserrat_28);
        lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(msg);
        return;
    }

    if (s_shown < 0 || s_shown >= (int)s_n_albums)
        s_shown = 0;

    if (s_n_albums > 1)
    {
        lv_obj_t *left = make_button(s_content, LV_SYMBOL_LEFT, ARROW_W, ARROW_H, COL_BLUE, ACT_ALBUM_PREV,
                                     &lv_font_montserrat_36);
        lv_obj_align(left, LV_ALIGN_LEFT_MID, ARROW_MARGIN, 0);
        s_arrows[0] = left;
        lv_obj_t *right = make_button(s_content, LV_SYMBOL_RIGHT, ARROW_W, ARROW_H, COL_BLUE, ACT_ALBUM_NEXT,
                                      &lv_font_montserrat_36);
        lv_obj_align(right, LV_ALIGN_RIGHT_MID, -ARROW_MARGIN, 0);
        s_arrows[1] = right;
    }

    show_album(s_shown);
    if (s_shuffle_btn)
        update_shuffle_button();        /* arrows hidden or not */
}

/* ---------- control bar ---------- */

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, int width, int height, uint32_t color, action_t act,
                             const lv_font_t *font)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, width, height);
    lv_obj_set_style_radius(btn, 24, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_50, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, on_button, LV_EVENT_CLICKED, (void *)(intptr_t)act);

    lv_obj_t *label = make_label(btn, text, font);
    lv_obj_center(label);
    return btn;
}

static lv_obj_t *make_bar_button(lv_obj_t *bar, const char *symbol, int width, uint32_t color, action_t act)
{
    lv_obj_t *btn = make_button(bar, symbol, width, BTN_H, color, act, &lv_font_montserrat_36);
    return lv_obj_get_child(btn, 0);    /* the label */
}

static void create_controls(lv_obj_t *scr)
{
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, LV_PCT(100), BAR_H);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0b0e13), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 8, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    make_bar_button(bar, LV_SYMBOL_PREV, 100, COL_BLUE, ACT_PREV);
    s_play_label = make_bar_button(bar, LV_SYMBOL_PLAY, 130, COL_GREEN, ACT_TOGGLE);
    make_bar_button(bar, LV_SYMBOL_NEXT, 100, COL_BLUE, ACT_NEXT);
    make_bar_button(bar, LV_SYMBOL_STOP, 100, COL_RED, ACT_STOP);

    make_bar_button(bar, LV_SYMBOL_MINUS, 110, COL_BLUE, ACT_VOL_DOWN);
    make_bar_button(bar, LV_SYMBOL_PLUS, 110, COL_BLUE, ACT_VOL_UP);

    /* shuffle: icon only, below the "previous album" arrow */
    s_shuffle_btn = make_button(scr, LV_SYMBOL_SHUFFLE, 80, 52, COL_CARD, ACT_SHUFFLE, &lv_font_montserrat_36);
    lv_obj_align(s_shuffle_btn, LV_ALIGN_TOP_LEFT, ARROW_MARGIN + (ARROW_W - 80) / 2, SUNTON_ESP32_LCD_HEIGHT - BAR_H - 60);
    update_shuffle_button();

    /* speaker icon and level bar: right of the cover, below the "next album" arrow */
    int x0 = SUNTON_ESP32_LCD_WIDTH - ARROW_MARGIN - ARROW_W;
    lv_obj_t *icon = make_label(scr, LV_SYMBOL_VOLUME_MID, &lv_font_montserrat_28);
    lv_obj_align(icon, LV_ALIGN_TOP_LEFT, x0, SUNTON_ESP32_LCD_HEIGHT - BAR_H - 34);
    s_vol_bar = lv_bar_create(scr);
    lv_bar_set_range(s_vol_bar, 0, PLAYER_VOLUME_MAX);
    lv_obj_set_size(s_vol_bar, ARROW_W - 52, 18);
    lv_obj_align(s_vol_bar, LV_ALIGN_TOP_LEFT, x0 + 52, SUNTON_ESP32_LCD_HEIGHT - BAR_H - 28);
    lv_obj_set_style_bg_color(s_vol_bar, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_bg_color(s_vol_bar, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
    lv_bar_set_value(s_vol_bar, player_volume(), LV_ANIM_OFF);     /* shown from the start, not only after the first change */
}

static void create_header(lv_obj_t *scr)
{
    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 60, 44);
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -GRID_PAD, 6);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COL_BLUE), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_50, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, on_button, LV_EVENT_CLICKED, (void *)(intptr_t)ACT_WIFI);
    lv_obj_t *label = make_label(btn, LV_SYMBOL_WIFI, &lv_font_montserrat_36);
    lv_obj_center(label);
}

/* ---------- periodic update ---------- */

static void poll(lv_timer_t *t)
{
    static int last_state = -1, last_track = -1, last_volume = -1, last_current = -2;
    static uint32_t done_at, advanced_at;

    /* upload page finished: leave upload mode shortly after the reply went out */
    if (s_upload_box)
    {
        if (s_upload_done && !done_at)
            done_at = lv_tick_get();
        if (done_at && lv_tick_elaps(done_at) > DONE_DELAY_MS)
        {
            done_at = 0;
            upload_leave();
            last_state = -1;    /* force a refresh of the new cards */
        }
        return;
    }

    /* no card at boot (or pulled out): look again every few seconds and show the music as soon as it is there */
    static uint32_t sd_checked_at;
    if (!sd_card_is_mounted() && lv_tick_elaps(sd_checked_at) > SD_RETRY_MS)
    {
        sd_checked_at = lv_tick_get();
        if (sd_card_mount() == ESP_OK && library_scan() == ESP_OK)
        {
            size_t n = 0;
            s_albums = library_albums(&n);
            s_n_albums = n;
            s_current = -1;
            lv_image_cache_drop(NULL);
            lv_image_header_cache_drop(NULL);
            build_library();
            last_state = -1;
        }
    }

    if (s_autostart && lv_tick_elaps(s_browse_tick) > AUTOSTART_MS)
    {
        s_autostart = false;
        s_current = s_shown;
        player_play_album(&s_albums[s_shown], 0);
    }

    int state = s_ev_state, track = s_ev_track, volume = s_ev_volume;

    /* shuffle over all albums: the screen shows the album of the track that plays */
    if (s_shuffle_on && state != PLAYER_STOPPED)
    {
        int ai = player_album_index();
        if (ai >= 0 && ai < (int)s_n_albums && (ai != s_current || ai != s_shown))
        {
            s_current = ai;
            if (ai != s_shown)
                show_album(ai);
        }
    }

    /* The player stopped by itself (last track of the album done; Stop, browsing and WiFi clear s_current first):
     * continue with the next album, after the last one with the first. Endless play. */
    if (last_state == PLAYER_PLAYING && state == PLAYER_STOPPED && s_current >= 0 && s_n_albums &&
        (!advanced_at || lv_tick_elaps(advanced_at) > 1500))
    {
        int next = s_current;
        for (size_t i = 0; i < s_n_albums; i++)
        {
            next = (next + 1) % (int)s_n_albums;
            if (s_albums[next].n_tracks)
                break;
        }
        if (s_albums[next].n_tracks)
        {
            s_current = next;
            show_album(next);
            player_play_album(&s_albums[next], 0);
            last_state = PLAYER_PLAYING;    /* the stop was handled; the next event is the new album's */
            advanced_at = lv_tick_get() | 1;
            return;
        }
    }
    if (volume != last_volume && volume >= 0)
    {
        lv_bar_set_value(s_vol_bar, volume, LV_ANIM_ON);
        last_volume = volume;
    }
    if (state == last_state && track == last_track && s_current == last_current && !s_dirty)
        return;
    last_state = state;
    last_track = track;
    last_current = s_current;
    s_dirty = false;

    lv_label_set_text(s_play_label, state == PLAYER_PLAYING ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);

    if (!s_card)
        return;
    bool active = state != PLAYER_STOPPED && s_shown == s_current;
    lv_obj_set_style_border_width(s_card, active ? 8 : 0, 0);
    if (active && track >= 0 && (size_t)track < s_albums[s_shown].n_tracks)
    {
        char name[128];
        song_title(s_albums[s_shown].tracks[track], name, sizeof(name));
        lv_label_set_text(s_title, name);
    }
    else
    {
        lv_label_set_text(s_title, s_albums[s_shown].title);
    }
    if (active)
    {
        lv_label_set_text_fmt(s_badge, "%s %d/%u", state == PLAYER_PLAYING ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE,
                              track + 1, (unsigned)s_albums[s_shown].n_tracks);
        lv_obj_remove_flag(s_badge, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(s_badge, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_create(const library_album_t *albums, size_t n_albums)
{
    s_albums = albums;
    s_n_albums = n_albums;

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    create_controls(scr);
    create_header(scr);
    build_library();

    lv_indev_t *indev = lv_indev_get_next(NULL);
    if (indev)
        lv_indev_set_scroll_limit(indev, SCROLL_LIMIT);

    lv_timer_create(poll, POLL_MS, NULL);
}
