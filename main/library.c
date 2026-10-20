#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "library.h"
#include "sd_card.h"

static const char *TAG = "library";

static library_album_t *s_albums;
static size_t s_count;

static bool is_mp3(const char *name)
{
    size_t n = strlen(name);
    return name[0] != '.' && n > 4 && strcasecmp(name + n - 4, ".mp3") == 0;
}

/* Library strings live in PSRAM: hundreds of small blocks would otherwise fill the internal RAM that WiFi and DMA need. */
static void *lib_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    return p ? p : malloc(n);
}

static char *join(const char *a, const char *b)
{
    size_t n = strlen(a) + strlen(b) + 2;
    char *s = lib_alloc(n);
    if (s)
        snprintf(s, n, "%s/%s", a, b);
    return s;
}

static int cmp_str(const void *a, const void *b)
{
    return strcasecmp(*(char *const *)a, *(char *const *)b);
}

static int cmp_album(const void *a, const void *b)
{
    return strcasecmp(((const library_album_t *)a)->title, ((const library_album_t *)b)->title);
}

static void free_albums(void)
{
    for (size_t i = 0; i < s_count; i++)
    {
        free(s_albums[i].title);
        free(s_albums[i].cover);
        for (size_t t = 0; t < s_albums[i].n_tracks; t++)
            free(s_albums[i].tracks[t]);
        free(s_albums[i].tracks);
    }
    free(s_albums);
    s_albums = NULL;
    s_count = 0;
}

/* Fills 'album' from 'dir_path'; returns false if the folder has no mp3 (nothing is left allocated). */
static bool read_album(const char *dir_path, const char *title, library_album_t *album)
{
    DIR *dir = opendir(dir_path);
    if (!dir)
        return false;

    memset(album, 0, sizeof(*album));
    size_t cap = 0;
    struct dirent *e;
    while ((e = readdir(dir)) != NULL)
    {
        if (!is_mp3(e->d_name))
            continue;
        if (album->n_tracks == cap)
        {
            cap = cap ? cap * 2 : 16;
            char **grown = realloc(album->tracks, cap * sizeof(char *));
            if (!grown)
                break;
            album->tracks = grown;
        }
        char *path = join(dir_path, e->d_name);
        if (path)
            album->tracks[album->n_tracks++] = path;
    }
    closedir(dir);

    if (album->n_tracks == 0)
    {
        free(album->tracks);
        return false;
    }
    qsort(album->tracks, album->n_tracks, sizeof(char *), cmp_str);

    album->title = lib_alloc(strlen(title) + 1);
    if (album->title)
        strcpy(album->title, title);
    char *cover = join(dir_path, "cover.jpg");
    struct stat st;
    if (cover && stat(cover, &st) == 0)
        album->cover = cover;
    else
        free(cover);
    return true;
}

esp_err_t library_scan(void)
{
    free_albums();

    DIR *root = opendir(SD_MOUNT_POINT);
    if (!root)
    {
        ESP_LOGE(TAG, "cannot open " SD_MOUNT_POINT);
        return ESP_FAIL;
    }

    size_t cap = 0;
    struct dirent *e;
    while ((e = readdir(root)) != NULL)
    {
        if (e->d_type != DT_DIR || e->d_name[0] == '.')
            continue;
        if (s_count == cap)
        {
            cap = cap ? cap * 2 : 32;
            library_album_t *grown = realloc(s_albums, cap * sizeof(library_album_t));
            if (!grown)
                break;
            s_albums = grown;
        }
        char *dir_path = join(SD_MOUNT_POINT, e->d_name);
        if (dir_path && read_album(dir_path, e->d_name, &s_albums[s_count]))
            s_count++;
        free(dir_path);
    }
    closedir(root);

    if (s_count)
        qsort(s_albums, s_count, sizeof(library_album_t), cmp_album);
    ESP_LOGI(TAG, "%u albums", (unsigned)s_count);
    return ESP_OK;
}

const library_album_t *library_albums(size_t *count)
{
    *count = s_count;
    return s_albums;
}
