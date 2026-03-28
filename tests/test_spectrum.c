#include <stdio.h>
#include <math.h>
#include <string.h>
#include "../lib/src/spectrum.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SAMPLE_RATE 44100
#define BUF_SIZE 1024

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  %-40s ", #name); \
    tests_run++; \
    if (test_##name()) { tests_passed++; printf("PASS\n"); } \
    else { printf("FAIL\n"); } \
} while(0)

static void gen_sine(float *buf, int count, float freq, float amplitude)
{
    for (int i = 0; i < count; i++)
        buf[i] = amplitude * sinf(2.0f * (float)M_PI * freq * (float)i / SAMPLE_RATE);
}

static int test_silence(void)
{
    f87_spectrum_ctx_t ctx;
    f87_spectrum_init(&ctx);

    float samples[BUF_SIZE] = {0};
    f87_audio_data_t out;
    /* Feed enough samples for FFT */
    for (int i = 0; i < 3; i++)
        f87_spectrum_analyze_ctx(&ctx, samples, BUF_SIZE, &out);

    f87_spectrum_destroy(&ctx);
    return out.energy < 0.01f && out.beat == false;
}

static int test_bass_detection(void)
{
    f87_spectrum_ctx_t ctx;
    f87_spectrum_init(&ctx);

    float samples[BUF_SIZE];
    gen_sine(samples, BUF_SIZE, 100.0f, 0.9f);

    f87_audio_data_t out;
    for (int i = 0; i < 4; i++)
        f87_spectrum_analyze_ctx(&ctx, samples, BUF_SIZE, &out);

    f87_spectrum_destroy(&ctx);
    return out.bands[1] > 0.1f && out.bands[1] > out.bands[5];
}

static int test_treble_detection(void)
{
    f87_spectrum_ctx_t ctx;
    f87_spectrum_init(&ctx);

    float samples[BUF_SIZE];
    gen_sine(samples, BUF_SIZE, 8000.0f, 0.9f);

    f87_audio_data_t out;
    for (int i = 0; i < 4; i++)
        f87_spectrum_analyze_ctx(&ctx, samples, BUF_SIZE, &out);

    f87_spectrum_destroy(&ctx);
    return out.bands[5] > 0.1f && out.bands[5] > out.bands[0];
}

static int test_beat_on_spike(void)
{
    f87_spectrum_ctx_t ctx;
    f87_spectrum_init(&ctx);

    float silence[BUF_SIZE] = {0};
    float loud[BUF_SIZE];
    gen_sine(loud, BUF_SIZE, 80.0f, 1.0f);

    f87_audio_data_t out;
    for (int i = 0; i < 20; i++)
        f87_spectrum_analyze_ctx(&ctx, silence, BUF_SIZE, &out);

    f87_spectrum_analyze_ctx(&ctx, loud, BUF_SIZE, &out);
    /* May need a couple more to fill FFT buffer */
    if (!out.beat) {
        f87_spectrum_analyze_ctx(&ctx, loud, BUF_SIZE, &out);
    }

    f87_spectrum_destroy(&ctx);
    return out.beat == true && out.beat_intensity > 0.0f;
}

static int test_no_beat_on_constant(void)
{
    f87_spectrum_ctx_t ctx;
    f87_spectrum_init(&ctx);

    float steady[BUF_SIZE];
    gen_sine(steady, BUF_SIZE, 80.0f, 0.5f);

    f87_audio_data_t out;
    for (int i = 0; i < 40; i++)
        f87_spectrum_analyze_ctx(&ctx, steady, BUF_SIZE, &out);

    f87_spectrum_destroy(&ctx);
    return out.beat == false;
}

int main(void)
{
    printf("Spectrum analysis tests:\n");
    TEST(silence);
    TEST(bass_detection);
    TEST(treble_detection);
    TEST(beat_on_spike);
    TEST(no_beat_on_constant);
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
