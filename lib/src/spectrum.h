#ifndef F87_SPECTRUM_H
#define F87_SPECTRUM_H

#include "f87/audio_types.h"
#include "kiss_fftr.h"

#define F87_FFT_SIZE     2048
#define F87_BEAT_HISTORY 22    /* ~500ms at 43Hz audio rate */
#define F87_BEAT_COOLDOWN_FRAMES 5  /* ~115ms at 43Hz */

typedef struct {
    kiss_fftr_cfg fft_cfg;
    float window[F87_FFT_SIZE];
    float fft_input[F87_FFT_SIZE];
    int sample_pos;

    /* Beat detection state */
    float bass_history[F87_BEAT_HISTORY];
    int bass_history_idx;
    int beat_cooldown;
} f87_spectrum_ctx_t;

void f87_spectrum_init(f87_spectrum_ctx_t *ctx);
void f87_spectrum_analyze_ctx(f87_spectrum_ctx_t *ctx, const float *samples,
                               int count, f87_audio_data_t *out);
void f87_spectrum_analyze(const float *samples, int count, f87_audio_data_t *out);
void f87_spectrum_destroy(f87_spectrum_ctx_t *ctx);

#endif /* F87_SPECTRUM_H */
