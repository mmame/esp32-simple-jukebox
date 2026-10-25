#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "settings.h"
#include "volume_pot.h"

static const char *TAG = "volume_pot";

#define REG_CONVERSION  0x00
#define REG_CONFIG      0x01
/* start one conversion, +-4.096 V, single shot, 128 SPS, comparator off; the channel (MUX) is added */
#define CONFIG_BASE     0x8383
#define MUX_AIN0        0x4000
#define MUX_AIN1        0x5000
#define CONVERSION_MS   10      /* 128 SPS = 7.8 ms */
#define I2C_TIMEOUT_MS  50
#define VOLT_PER_COUNT  (4.096f / 32768.0f)

/* Reading with the pot at 3.3 V is about 26400 (32767 = 4.096 V); keep a small dead zone at both ends so the
 * extremes are reached reliably whatever the supply voltage. */
#define POT_RAW_MIN     400
#define POT_RAW_MAX     25600

#define BATT_DIVIDER        2.0f    /* VBat = 2 x measured */
#define BATT_MIN_VOLTS      2.5f    /* below: no battery connected */
#define BATT_MAX_CELL_VOLTS 4.6f    /* above: not a Li-ion cell, external supply */
#define BATT_EVERY          4       /* battery every 4th loop (200 ms), pot every loop (50 ms) */
#define BATT_ALPHA          0.03f   /* smoothing: the amplifier load makes the voltage jump with the music */

static i2c_master_dev_handle_t s_dev;
static volatile int s_pot_raw = -1;
static volatile float s_batt_volts = -1;
static volatile float s_batt_instant = -1;

/* Charging is guessed from the voltage trend: a charger lifts the terminal voltage by tens of mV at once and on, while
 * discharging it falls about 1 mV per minute. The smoothed voltage is sampled every few seconds into a window; "charging"
 * is when it is clearly above the lowest value of the window, "not charging" when it falls clearly below the highest. */
#define CHG_SAMPLE_MS       5000
#define CHG_WINDOW          24          /* 2 minutes */
#define CHG_MIN_SAMPLES     3
#define CHG_RISE_VOLTS      0.040f
#define CHG_FALL_VOLTS      0.030f
#define CHG_JUMP_VOLTS      0.050f      /* a jump this big counts at once, even right after a load change */
#define CHG_HOLDOFF_MS     40000       /* after a load change: the smoothed voltage needs this long to settle */

static volatile bool s_charging;
static volatile uint32_t s_load_changed_at;
static float s_win[CHG_WINDOW];
static int s_win_n, s_win_head;
static TickType_t s_win_tick;
static uint32_t s_seen_load_change;

static bool s_pot_first = true;
static float s_pot_filtered;
static int s_pot_step = -1;

/* Typical 18650 under a light load, voltage -> percent. */
static const struct { float volts; float percent; } k_curve[] = {
    { 3.27f, 0 },  { 3.61f, 5 },  { 3.69f, 10 }, { 3.71f, 15 }, { 3.73f, 20 }, { 3.75f, 25 }, { 3.77f, 30 },
    { 3.79f, 35 }, { 3.80f, 40 }, { 3.82f, 45 }, { 3.84f, 50 }, { 3.85f, 55 }, { 3.87f, 60 }, { 3.91f, 65 },
    { 3.95f, 70 }, { 3.98f, 75 }, { 4.02f, 80 }, { 4.08f, 85 }, { 4.11f, 90 }, { 4.15f, 95 }, { 4.20f, 100 },
};

static int read_channel(uint16_t mux)
{
    const uint16_t cfg = CONFIG_BASE | mux;
    const uint8_t w[3] = { REG_CONFIG, cfg >> 8, cfg & 0xff };
    if (i2c_master_transmit(s_dev, w, sizeof(w), I2C_TIMEOUT_MS) != ESP_OK)
        return INT32_MIN;
    vTaskDelay(pdMS_TO_TICKS(CONVERSION_MS));
    const uint8_t reg = REG_CONVERSION;
    uint8_t rx[2];
    if (i2c_master_transmit_receive(s_dev, &reg, 1, rx, sizeof(rx), I2C_TIMEOUT_MS) != ESP_OK)
        return INT32_MIN;
    return (int16_t)((rx[0] << 8) | rx[1]);
}

static void win_reset(float v)
{
    s_win[0] = v;
    s_win_n = 1;
    s_win_head = 1 % CHG_WINDOW;
    s_win_tick = xTaskGetTickCount();
}

/* Called with every smoothed battery voltage; v < 0 = no battery. */
static void charge_detect(float v)
{
    const TickType_t now = xTaskGetTickCount();
    if (v < 0)
    {
        s_charging = false;
        s_win_n = 0;
        return;
    }
    if (s_win_n == 0)
        win_reset(v);

    /* the load (music, backlight) changed: the voltage steps without any charger. Start over, keep the state. */
    static TickType_t holdoff_until;
    static bool holding;
    if (s_load_changed_at != s_seen_load_change)
    {
        s_seen_load_change = s_load_changed_at;
        holdoff_until = now + pdMS_TO_TICKS(CHG_HOLDOFF_MS);
        holding = true;
    }
    if (holding)
    {
        if ((int32_t)(now - holdoff_until) < 0)
        {
            /* The voltage is still settling after a load change (the window keeps the samples from before it), but a
             * big jump is the charger: a load change moves it by 10..20 mV, a charger by 100 mV. */
            if (s_win_n)
            {
                float lo = s_win[0], hi = s_win[0];
                for (int i = 1; i < s_win_n; i++)
                {
                    lo = s_win[i] < lo ? s_win[i] : lo;
                    hi = s_win[i] > hi ? s_win[i] : hi;
                }
                const bool plugged = !s_charging && v - lo > CHG_JUMP_VOLTS;
                const bool unplugged = s_charging && hi - v > CHG_JUMP_VOLTS;
                if (plugged || unplugged)
                {
                    s_charging = plugged;
                    holding = false;
                    win_reset(v);
                    ESP_LOGI(TAG, "charging %s (%.3f V, big jump)", plugged ? "detected" : "ended", v);
                }
            }
            return;
        }
        holding = false;
        win_reset(v);
        return;
    }
    if (now - s_win_tick < pdMS_TO_TICKS(CHG_SAMPLE_MS))
        return;
    s_win_tick = now;
    s_win[s_win_head] = v;
    s_win_head = (s_win_head + 1) % CHG_WINDOW;
    if (s_win_n < CHG_WINDOW)
        s_win_n++;
    if (s_win_n < CHG_MIN_SAMPLES)
        return;

    float lo = s_win[0], hi = s_win[0];
    for (int i = 1; i < s_win_n; i++)
    {
        lo = s_win[i] < lo ? s_win[i] : lo;
        hi = s_win[i] > hi ? s_win[i] : hi;
    }
    if (!s_charging && v - lo > CHG_RISE_VOLTS)
    {
        s_charging = true;
        win_reset(v);               /* from now on a fall is measured from here */
        ESP_LOGI(TAG, "charging detected (%.3f V)", v);
    }
    else if (s_charging && hi - v > CHG_FALL_VOLTS)
    {
        s_charging = false;
        win_reset(v);
        ESP_LOGI(TAG, "charging ended (%.3f V)", v);
    }
}

static void adc_task(void *arg)
{
    for (unsigned n = 0;; n++)
    {
        int raw = read_channel(MUX_AIN0);
        if (raw != INT32_MIN)
            s_pot_raw = raw;

        if (n % BATT_EVERY == 0)
        {
            raw = read_channel(MUX_AIN1);
            if (raw != INT32_MIN)
            {
                float v = raw * VOLT_PER_COUNT * BATT_DIVIDER;
                s_batt_instant = v;
                float old = s_batt_volts;
                if (v < BATT_MIN_VOLTS)
                    s_batt_volts = -1;      /* no battery: forget the average at once, no icon / warning / shutdown */
                else
                    s_batt_volts = old < 0 ? v : old + (v - old) * BATT_ALPHA;
                charge_detect(s_batt_volts);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

bool volume_pot_init(i2c_master_bus_handle_t bus)
{
    if (i2c_master_probe(bus, VOLUME_POT_I2C_ADDR, I2C_TIMEOUT_MS) != ESP_OK)
    {
        ESP_LOGI(TAG, "no ADS1115 at 0x%02x, volume only with the touch buttons, no battery icon", VOLUME_POT_I2C_ADDR);
        return false;
    }
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = VOLUME_POT_I2C_ADDR,
        .scl_speed_hz = 100000,
    };
    if (i2c_master_bus_add_device(bus, &cfg, &s_dev) != ESP_OK)
        return false;
    if (xTaskCreatePinnedToCore(adc_task, "adc", 3072, NULL, 2, NULL, 1) != pdPASS)
    {
        s_dev = NULL;
        return false;
    }
    ESP_LOGI(TAG, "ADS1115 found: volume pot on AIN0, battery on AIN1");
    return true;
}

bool volume_pot_present(void)
{
    return s_dev != NULL;
}

bool volume_pot_changed(int max, int *step)
{
    int raw = s_pot_raw;
    if (!s_dev || (raw < 0 && s_pot_first))     /* no reading yet */
        return false;

    s_pot_filtered = s_pot_first ? (float)raw : s_pot_filtered + ((float)raw - s_pot_filtered) / 3.0f;

    float pos = (s_pot_filtered - POT_RAW_MIN) / (float)(POT_RAW_MAX - POT_RAW_MIN);
    pos = pos < 0 ? 0 : (pos > 1 ? 1 : pos);
    float exact = pos * max;

    /* hysteresis: move on only when clearly past the border between two steps */
    if (s_pot_first || fabsf(exact - (float)s_pot_step) > 0.75f)
    {
        int next = (int)lroundf(exact);
        s_pot_first = false;
        if (next != s_pot_step)
        {
            s_pot_step = next;
            *step = next;
            return true;
        }
    }
    return false;
}

bool battery_charging(void)
{
    return s_dev != NULL && s_batt_volts >= BATT_MIN_VOLTS && s_charging;
}

void battery_load_changed(void)
{
    s_load_changed_at++;
}

/* The measurement is stored as it comes from the divider; the user's correction is applied when it is read. */
static float calibration(void)
{
    return settings_batt_cal() / 10000.0f;
}

bool battery_read_instant(float *volts)
{
    float v = s_batt_instant;
    if (!s_dev || v < 0)
        return false;
    *volts = v * calibration();
    return true;
}

bool battery_read(int *percent, float *volts, bool *external)
{
    float v = s_batt_volts;
    if (!s_dev || v < BATT_MIN_VOLTS)
        return false;
    v *= calibration();
    *volts = v;
    *external = v > BATT_MAX_CELL_VOLTS;

    float p = 100;
    if (v <= k_curve[0].volts)
    {
        p = 0;
    }
    else
    {
        const int n = sizeof(k_curve) / sizeof(k_curve[0]);
        for (int i = 1; i < n; i++)
            if (v <= k_curve[i].volts)
            {
                float f = (v - k_curve[i - 1].volts) / (k_curve[i].volts - k_curve[i - 1].volts);
                p = k_curve[i - 1].percent + f * (k_curve[i].percent - k_curve[i - 1].percent);
                break;
            }
    }
    *percent = (int)lroundf(p);
    return true;
}
