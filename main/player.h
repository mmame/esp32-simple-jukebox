/**
 * @file player.h
 * @brief MP3 playback engine: decodes with libhelix and plays over I2S. All calls are non-blocking
 *        and may be made from any task (e.g. LVGL event handlers); they are executed by the player task.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "library.h"

#define PLAYER_VOLUME_MAX 20

typedef enum {
    PLAYER_STOPPED,
    PLAYER_PLAYING,
    PLAYER_PAUSED,
} player_state_t;

/** Called from the player task whenever state, track or volume changes. */
typedef void (*player_event_cb_t)(player_state_t state, size_t track, int volume);

esp_err_t player_init(player_event_cb_t cb);

/** Starts playing 'album' at 'track'. The album must stay valid (no library_scan) while it plays. */
void player_play_album(const library_album_t *album, size_t track);
/** Pause <-> resume; starts the last album again when stopped. */
void player_toggle_pause(void);
void player_stop(void);
/** Shuffle mode: all tracks of all albums play in random order (every track once, then reshuffled). Can be switched
 *  while playing; the track that plays stays first. Needs player_set_library(). */
void player_set_shuffle(bool on);
/** Tells the player the library (call again after every library_scan(), the old pointers become invalid). */
void player_set_library(const library_album_t *albums, size_t n);
/** Index of the album the current track belongs to (follows the shuffle), -1 if none. */
int player_album_index(void);
/** Next/previous track of the album (wraps around), starts playing. */
void player_next(void);
void player_prev(void);
void player_volume_up(void);
void player_volume_down(void);
/** Sets the volume directly, 0..PLAYER_VOLUME_MAX (volume pot). */
void player_set_volume(int level);
int player_volume(void);
/** Stops and forgets the album; blocks until done. Call before library_scan(), which frees the album. */
void player_reset(void);
