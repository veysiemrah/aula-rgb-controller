#ifdef F87_HAS_AUDIO

#include "spectrum.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SAMPLE_RATE 44100

static const float band_edges[F87_AUDIO_BANDS + 1] = {
    20.0f, 60.0f, 250.0f, 500.0f, 2000.0f, 4000.0f, 16000.0f
};

void f87_spectrum_init(f87_spectrum_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->fft_cfg = kiss_fftr_alloc(F87_FFT_SIZE, 0, NULL, NULL);

    for (int i = 0; i < F87_FFT_SIZE; i++)
        ctx->window[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * (float)i / (float)(F87_FFT_SIZE - 1)));

    ctx->sample_pos = 0;
    ctx->bass_history_idx = 0;
    ctx->beat_cooldown = 0;
}

void f87_spectrum_destroy(f87_spectrum_ctx_t *ctx)
{
    if (ctx->fft_cfg) {
        kiss_fftr_free(ctx->fft_cfg);
        ctx->fft_cfg = NULL;
    }
}

void f87_spectrum_analyze_ctx(f87_spectrum_ctx_t *ctx, const float *samples,
                               int count, f87_audio_data_t *out)
{
    memset(out, 0, sizeof(*out));

    int to_copy = count;
    if (ctx->sample_pos + to_copy > F87_FFT_SIZE)
        to_copy = F87_FFT_SIZE - ctx->sample_pos;

    memcpy(ctx->fft_input + ctx->sample_pos, samples, to_copy * sizeof(float));
    ctx->sample_pos += to_copy;

    if (ctx->sample_pos < F87_FFT_SIZE) return;

    float windowed[F87_FFT_SIZE];
    for (int i = 0; i < F87_FFT_SIZE; i++)
        windowed[i] = ctx->fft_input[i] * ctx->window[i];

    memmove(ctx->fft_input, ctx->fft_input + F87_FFT_SIZE / 2,
            (F87_FFT_SIZE / 2) * sizeof(float));
    ctx->sample_pos = F87_FFT_SIZE / 2;

    kiss_fft_cpx spectrum[F87_FFT_SIZE / 2 + 1];
    kiss_fftr(ctx->fft_cfg, windowed, spectrum);

    int num_bins = F87_FFT_SIZE / 2 + 1;
    float band_energy[F87_AUDIO_BANDS] = {0};
    int band_count[F87_AUDIO_BANDS] = {0};
    float total_energy = 0.0f;

    for (int i = 1; i < num_bins; i++) {
        float freq = (float)i * (float)SAMPLE_RATE / (float)F87_FFT_SIZE;
        float mag = sqrtf(spectrum[i].r * spectrum[i].r + spectrum[i].i * spectrum[i].i);
        mag /= (float)(F87_FFT_SIZE / 2);

        total_energy += mag * mag;

        for (int b = 0; b < F87_AUDIO_BANDS; b++) {
            if (freq >= band_edges[b] && freq < band_edges[b + 1]) {
                band_energy[b] += mag;
                band_count[b]++;
                break;
            }
        }
    }

    float max_band = 0.0f;
    for (int b = 0; b < F87_AUDIO_BANDS; b++) {
        if (band_count[b] > 0)
            band_energy[b] /= (float)band_count[b];
        if (band_energy[b] > max_band)
            max_band = band_energy[b];
    }

    if (max_band > 0.0f) {
        for (int b = 0; b < F87_AUDIO_BANDS; b++)
            out->bands[b] = band_energy[b] / max_band;
    }

    out->energy = sqrtf(total_energy / (float)num_bins);
    if (out->energy > 1.0f) out->energy = 1.0f;

    float bass = (band_energy[0] + band_energy[1]) * 0.5f;

    ctx->bass_history[ctx->bass_history_idx % F87_BEAT_HISTORY] = bass;
    ctx->bass_history_idx++;

    float avg_bass = 0.0f;
    int hist_count = ctx->bass_history_idx < F87_BEAT_HISTORY
                     ? ctx->bass_history_idx : F87_BEAT_HISTORY;
    for (int i = 0; i < hist_count; i++)
        avg_bass += ctx->bass_history[i];
    avg_bass /= (float)hist_count;

    if (ctx->beat_cooldown > 0) {
        ctx->beat_cooldown--;
    } else if (hist_count >= 3 && bass > avg_bass * 1.5f && bass > 0.001f) {
        out->beat = true;
        out->beat_intensity = (bass - avg_bass) / (avg_bass + 0.001f);
        if (out->beat_intensity > 1.0f) out->beat_intensity = 1.0f;
        ctx->beat_cooldown = F87_BEAT_COOLDOWN_FRAMES;
    }
}

static pthread_key_t tl_ctx_key;
static pthread_once_t tl_ctx_once = PTHREAD_ONCE_INIT;

static void tl_ctx_destructor(void *p)
{
    f87_spectrum_ctx_t *ctx = p;
    if (ctx) {
        f87_spectrum_destroy(ctx);
        free(ctx);
    }
}

static void tl_ctx_make_key(void)
{
    pthread_key_create(&tl_ctx_key, tl_ctx_destructor);
}

void f87_spectrum_analyze(const float *samples, int count, f87_audio_data_t *out)
{
    pthread_once(&tl_ctx_once, tl_ctx_make_key);

    f87_spectrum_ctx_t *ctx = pthread_getspecific(tl_ctx_key);
    if (!ctx) {
        ctx = calloc(1, sizeof(f87_spectrum_ctx_t));
        if (!ctx) {
            memset(out, 0, sizeof(*out));
            return;
        }
        f87_spectrum_init(ctx);
        pthread_setspecific(tl_ctx_key, ctx);
    }
    f87_spectrum_analyze_ctx(ctx, samples, count, out);
}

#endif /* F87_HAS_AUDIO */
