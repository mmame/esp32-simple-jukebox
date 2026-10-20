# esp32-simple-jukebox

MP3 player for the Sunton ESP32-8048S050C (ESP32-S3, 5" 800x480 RGB panel, GT911 touch, SD slot, I2S audio).

Made to be dead simple for older people: one big album cover on the screen, two big arrows to change album,
and only a few large buttons. Music is added from a phone or laptop through a built-in web page.

## Using the player

```
+--------------------------------------------------------------+
|                                                    [ WiFi ]  |
|   [ <- ]        +--------------------+          [ -> ]       |
|                 |                    |                       |
|                 |   album cover      |        (speaker) ===  |
|                 |   360 x 360        |                       |
|                 |  [ album title ]   |                       |
|                 +--------------------+                       |
|  [|<]  [ Play/Pause ]  [>|]  [Stop]   [ - ]  [ + ]           |
+--------------------------------------------------------------+
```

* **Tap the cover** to play the album from its first track. A frame and a badge ("track 3/12") mark the playing album.
* **Arrows left/right** change the album on screen. If music was playing it stops at once; if the arrows
  are not touched for 3 seconds, the album now on screen starts by itself.
* **Play / Prev / Next** start the album on screen when nothing is playing; otherwise they pause/resume and skip tracks.
* **Stop** stops playback. **- / +** change the volume (0..20, the bar above the buttons shows it, startup volume is 5).
* **WiFi button** (top right) is for adding music, see below.

## Adding music

WiFi is off during normal use. Tap the WiFi button: playback stops, the player opens an access point and shows a box with
the network name, the password and a QR code for joining.

| | |
| --- | --- |
| Network | `MyMusicPlayer` |
| Password | `music1234` |
| Web page | `http://192.168.4.1` |

(constants in `main/wifi_ap.h`). Open the page, then tap **Done** in the box on the player, or **Done** on the web page,
to switch WiFi off again. The library is rescanned and the covers reload.

The page is embedded in the firmware (`main/web/index.html`, no internet needed) and does all conversion in the browser:

* **Cover:** any picture, cropped to a square and scaled to 360x360 baseline JPEG.
* **Songs:** MPEG Layer III files are sent unchanged (ID3 tags stripped). Anything else (FLAC, WAV, M4A, OGG, ...) is
  decoded by the browser and encoded to 160 kbps MP3 with the bundled lamejs (`main/web/lame.min.js`, LGPL-3.0).
* Songs are stored as `NN_name.mp3` so the play order matches the order in the page. Use an existing album name to append songs.
* Per album: **Rename**, **Change cover**, **Add songs**, **Delete album**; single songs can be deleted too.
* **Firmware update:** the page installs the `-ota.bin` image. An image that is not an app image of this project
  (for example the full image) is rejected.

### Scripting / AI agents

`tools/musicplayer.py` (Python 3, standard library only) drives the same REST API from a computer on the player's WiFi.
Switch upload mode on first (tap the WiFi button). Every command prints one JSON line and exits non-zero on failure. Upload and OTA show progress on stderr
("[ 42%] 03_song.mp3 (2/5) 80%", overall percent plus the current file; one updating line in a terminal, one line per file
otherwise); `--quiet` turns it off.

    python tools/musicplayer.py upload --album "Beatles - Abbey Road" --cover cover.png 01.mp3 02.flac --done
    python tools/musicplayer.py upload --album "Beatles - Abbey Road" --dir ./abbey_road   # audio files + cover.* from a folder
    python tools/musicplayer.py list | rename | delete | ota | done

* Album title = folder name (forbidden characters are removed, max. 60). The cover is centre-cropped to a 360x360 baseline
  JPEG (needs Pillow; without it only a JPEG is sent as it is). MP3 files are sent unchanged, other audio is converted with
  `ffmpeg` if installed. Tracks are stored as `NN_name.mp3`; an existing album gets the new tracks appended (`--replace` starts fresh).
* Plain HTTP for your own client (host `192.168.4.1`): `PUT /api/file?album=<title>&file=cover.jpg` with the JPEG as the body,
  `PUT /api/file?album=<title>&file=01_song.mp3` with the MP3 as the body (the album folder is created on the first PUT),
  `GET /api/albums` lists everything, `POST /api/done` leaves upload mode. No login: the WPA2 password protects the access point.

REST API: see `main/web_server.h`. Uploads go to a `.part` file first, so an aborted upload leaves nothing broken behind.

## SD card layout

FAT32, one folder per album:

    /<Album name>/cover.jpg     square cover, optional (360x360 is shown 1:1, other sizes are stretched)
    /<Album name>/<track>.mp3   tracks, played in file name order (01_, 02_, ...)

The folder name is the title shown on the cover. Files at the card root are ignored. The web page creates this layout.

## Build and flash

ESP-IDF 5.5.x, target ESP32-S3:

    idf.py set-target esp32s3
    idf.py build flash monitor

`.\build_release.ps1` (PowerShell with the IDF environment) builds and writes two files to `release/`:

| File | Content | Use |
| --- | --- | --- |
| `esp32-cyd-musicplayer-full.bin` | bootloader + partition table + otadata + app | first flash over USB at **0x0**: `esptool.py --chip esp32s3 -p COMx write_flash 0x0 release\esp32-cyd-musicplayer-full.bin` |
| `esp32-cyd-musicplayer-ota.bin` | app only | update through the web page (written to the inactive slot) |

Every run of these scripts writes the current date and time to `build_stamp.txt`. It becomes the app version, shown on the
web page as "Firmware build 2026-10-20 14:44:17" and in the boot log (`App version`). A plain `idf.py build` keeps the last stamp.

`.\build_variant.ps1 <name>` builds an experiment with `sdkconfig.defaults` + `sdkconfig.<name>` into `build_variant_<name>\`
and `release\esp32-cyd-musicplayer-<name>-{ota,full}.bin`. Delete `sdkconfig` after editing `sdkconfig.defaults`.

Partition table (`partitions.csv`, 16 MB flash): nvs 0x9000, otadata 0xf000, phy_init 0x11000, ota_0 0x20000 (6 MB),
ota_1 0x620000 (6 MB); about 3.9 MB stay free. Changing this layout later needs a full USB flash.

## Pins

| Function | GPIO |
| --- | --- |
| SD CS / MOSI / SCLK / MISO | 10 / 11 / 12 / 13 |
| I2S BCLK / LRC / DOUT | 0 / 18 / 17 |
| Touch SDA / SCL / RST | 19 / 20 / 38 |
| Backlight | 2 |

The SD and I2S pins are the GPIOs left free by the RGB panel, taken from the Elecrow 5" ESP32-S3 wiki. The touch range is read
from the GT911 at startup, so the 4.3" ESP32-8048S043 should work too (untested).

## Display notes (hard-won)

The RGB framebuffer lives in PSRAM, which shares its bus with the panel DMA, WiFi and the code (it runs from PSRAM).
Bandwidth conflicts show up as brief sideways shifts of the picture. What works on this board:

* **Bounce buffer (default):** the panel DMA reads small internal-RAM buffers (10 lines) that an interrupt refills from the
  PSRAM framebuffers. This removed the sideways shifts. It needs `CONFIG_LCD_RGB_ISR_IRAM_SAFE=y`, so the flush callback is
  `IRAM_ATTR`, and 64-byte data cache lines. Two framebuffers, LVGL in direct mode.
* **LVGL task pinned to core 1:** core 0 runs the refill interrupt and WiFi.
* **`-O2`:** the IDF default `-Og` (debug) made drawing visibly slow.
* **Pixel clock must stay at 16 MHz.** 12 MHz makes the panel lose sync (it shows the RGBW test pattern); changing the clock at runtime too.
* `CONFIG_LCD_RGB_RESTART_IN_VSYNC=y` resyncs the DMA every vblank, so WiFi traffic can never leave a permanent shift.
* The MP3 decoder (libhelix) is linked into internal RAM (`main/linker.lf`) so decoding does not compete for PSRAM bandwidth.
* `CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384`: building the UI with 360 px covers overflows the default stack.

Tried and dropped: no bounce buffer (fast but the shifts return), a single framebuffer with partial rendering (far too slow),
flash and PSRAM at 120 MHz (does not boot on this unit, its flash is reported as "generic"), runtime pixel clock changes.

## Code map

* `main.c` startup; `sunton_esp32_8048s050c.c` board: panel, backlight, touch, LVGL port
* `sd_card.c` mounts the card at `/sdcard` (SPI); `library.c` scans albums and tracks
* `player.c` decode task (priority 5, core 1): play/pause, stop, next, prev, volume; calls are non-blocking
* `ui.c` one-album screen, arrows, control bar; the player task never takes the LVGL lock, the UI polls a state snapshot every 100 ms
* `wifi_ap.c`, `web_server.c`, `upload_mode.c`, `web/` upload mode (access point, REST API, web page)

## Status

Tested on hardware: display, touch, SD library, playback, web upload, album edit, glitch-free display with the bounce buffer.
Not yet verified: the OTA update flow, the 4.3" variant, SD pins on other board revisions. App rollback is not enabled.

## License

MIT, see `LICENSE`, with these exceptions:

* `main/sunton_esp32_8048s050c.c` and `.h` (board support: panel, touch, LVGL port) are based on
  [mr-sven/esp32-8048S050C](https://github.com/mr-sven/esp32-8048S050C) by Sven Fabricius (see the file headers) and are not
  covered by this license. That repository has no license file at the moment; if you know its terms, please open an issue.
* `main/web/lame.min.js` is lamejs, LGPL-3.0.
* Dependencies (ESP-IDF, LVGL, esp_lcd_touch_gt911, libhelix MP3) are downloaded by the build and keep their own licenses.
