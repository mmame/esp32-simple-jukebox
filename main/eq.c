#include <math.h>

#include "eq.h"
#include "settings.h"

#define HIGHPASS_HZ     80.0f
#define BASS_HZ         160.0f
#define TREBLE_HZ       6000.0f
#define SHELF_SLOPE     1.0f            /* RBJ shelf slope S: 1 = as steep as it gets without overshoot */

typedef struct {
    float b0, b1, b2, a1, a2;           /* normalised (a0 = 1) */
} coef_t;

typedef struct {
    float z1, z2;                       /* transposed direct form II */
} state_t;

enum { HIGHPASS, BASS, TREBLE, STAGES };

static coef_t s_coef[STAGES];
static state_t s_state[2][STAGES];      /* per channel */
static uint32_t s_rate;
static int s_bass = -1, s_treble = -1;

static coef_t normalise(float b0, float b1, float b2, float a0, float a1, float a2)
{
    return (coef_t){ b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

static coef_t passthrough(void)
{
    return (coef_t){ 1, 0, 0, 0, 0 };
}

/* Robert Bristow-Johnson's audio EQ cookbook */
static coef_t highpass(float fs, float f0)
{
    const float w0 = 2 * (float)M_PI * f0 / fs, cw = cosf(w0), alpha = sinf(w0) / (2 * 0.70710678f);
    return normalise((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
}

static coef_t shelf(float fs, float f0, float gain_db, bool high)
{
    if (gain_db <= 0 || f0 >= 0.45f * fs)
        return passthrough();
    const float A = powf(10.0f, gain_db / 40.0f);
    const float w0 = 2 * (float)M_PI * f0 / fs, cw = cosf(w0), sw = sinf(w0);
    const float alpha = sw / 2 * sqrtf((A + 1 / A) * (1 / SHELF_SLOPE - 1) + 2);
    const float t = 2 * sqrtf(A) * alpha;
    if (high)
        return normalise(A * ((A + 1) + (A - 1) * cw + t), -2 * A * ((A - 1) + (A + 1) * cw),
                         A * ((A + 1) + (A - 1) * cw - t), (A + 1) - (A - 1) * cw + t,
                         2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - t);
    return normalise(A * ((A + 1) - (A - 1) * cw + t), 2 * A * ((A - 1) - (A + 1) * cw),
                     A * ((A + 1) - (A - 1) * cw - t), (A + 1) + (A - 1) * cw + t,
                     -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - t);
}

static void update(uint32_t rate, int bass, int treble)
{
    const float fs = (float)rate;
    s_coef[HIGHPASS] = highpass(fs, HIGHPASS_HZ);
    s_coef[BASS] = shelf(fs, BASS_HZ, (float)bass, false);
    s_coef[TREBLE] = shelf(fs, TREBLE_HZ, (float)treble, true);
    s_rate = rate;
    s_bass = bass;
    s_treble = treble;
}

static inline float run(const coef_t *c, state_t *s, float x)
{
    const float y = c->b0 * x + s->z1;
    s->z1 = c->b1 * x - c->a1 * y + s->z2;
    s->z2 = c->b2 * x - c->a2 * y;
    return y;
}

/* Soft limiter: linear up to 80 % of full scale, then a smooth curve towards full scale instead of hard clipping. */
#define KNEE    26214.0f                /* 0.8 * 32767 */
#define ROOM    (32767.0f - KNEE)

static inline int16_t limit(float x)
{
    const float a = fabsf(x);
    if (a <= KNEE)
        return (int16_t)lrintf(x);
    const float y = KNEE + ROOM * tanhf((a - KNEE) / ROOM);
    return (int16_t)lrintf(x < 0 ? -y : y);
}

/* The tone control may push peaks above full scale; the volume (gain <= 1) is applied before the limiter, so only the
 * highest volume steps can reach it. */
static inline int16_t filter(int ch, int16_t in, float gain)
{
    float x = (float)in;
    for (int i = 0; i < STAGES; i++)
        x = run(&s_coef[i], &s_state[ch][i], x);
    return limit(x * gain);
}

void eq_process(int16_t *pcm, int frames, uint32_t rate, bool mono, int32_t gain_q15)
{
    const int bass = settings_eq_bass(), treble = settings_eq_treble();
    if (rate != s_rate || bass != s_bass || treble != s_treble)
        update(rate, bass, treble);
    const float gain = (float)gain_q15 / 32768.0f;

    if (mono)
    {
        for (int i = 0; i < frames; i++)
            pcm[2 * i] = pcm[2 * i + 1] = filter(0, pcm[2 * i], gain);
    }
    else
    {
        for (int i = 0; i < frames; i++)
        {
            pcm[2 * i] = filter(0, pcm[2 * i], gain);
            pcm[2 * i + 1] = filter(1, pcm[2 * i + 1], gain);
        }
    }
}
