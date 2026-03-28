#ifdef F87_HAS_AUDIO

#include "animate_internal.h"
#include "spectrum.h"
#include <pulse/simple.h>
#include <pulse/error.h>
#include <string.h>
#include <stdio.h>

#define F87_AUDIO_RATE      44100
#define F87_AUDIO_CHANNELS  1
#define F87_AUDIO_BUF_SIZE  1024

static void *audio_thread_func(void *arg)
{
    f87_anim_ctx_t *ctx = arg;
    int pa_err;

    pa_sample_spec ss = {
        .format   = PA_SAMPLE_FLOAT32LE,
        .rate     = F87_AUDIO_RATE,
        .channels = F87_AUDIO_CHANNELS,
    };

    /* For monitor mode: find the default sink and append .monitor.
     * For mic mode: NULL uses default input device. */
    const char *source = NULL;
    char monitor_name[256] = {0};

    if (ctx->config.audio_source == F87_AUDIO_MONITOR) {
        /* Find a RUNNING monitor source via pactl.
         * Falls back to default sink .monitor if pactl not available. */
        FILE *fp = popen("pactl list short sources 2>/dev/null | grep monitor | grep RUNNING | head -1 | awk '{print $2}'", "r");
        if (fp) {
            if (fgets(monitor_name, sizeof(monitor_name) - 1, fp)) {
                char *nl = strchr(monitor_name, '\n');
                if (nl) *nl = '\0';
                if (monitor_name[0] != '\0')
                    source = monitor_name;
            }
            pclose(fp);
        }
        /* Fallback: try default sink + .monitor */
        if (!source || monitor_name[0] == '\0') {
            fp = popen("pactl get-default-sink 2>/dev/null", "r");
            if (fp) {
                if (fgets(monitor_name, sizeof(monitor_name) - 9, fp)) {
                    char *nl = strchr(monitor_name, '\n');
                    if (nl) *nl = '\0';
                    strcat(monitor_name, ".monitor");
                    source = monitor_name;
                }
                pclose(fp);
            }
        }
        if (!source || monitor_name[0] == '\0')
            source = "alsa_output.pci-0000_00_1f.3.analog-stereo.monitor";

        fprintf(stderr, "f87: audio source: %s\n", source);
    }

    /* Low-latency buffer attributes */
    pa_buffer_attr ba = {
        .maxlength = (uint32_t)-1,
        .tlength   = (uint32_t)-1,
        .prebuf    = (uint32_t)-1,
        .minreq    = (uint32_t)-1,
        .fragsize  = F87_AUDIO_BUF_SIZE * sizeof(float),  /* ~23ms fragments */
    };

    pa_simple *pa = pa_simple_new(
        NULL,
        "f87control",
        PA_STREAM_RECORD,
        source,
        "audio-visualizer",
        &ss,
        NULL,
        &ba,
        &pa_err
    );

    if (!pa) {
        fprintf(stderr, "f87: PulseAudio error: %s\n", pa_strerror(pa_err));
        atomic_store(&ctx->error, F87_ERR_AUDIO);
        return NULL;
    }

    float samples[F87_AUDIO_BUF_SIZE];

    while (atomic_load(&ctx->running)) {
        if (pa_simple_read(pa, samples, sizeof(samples), &pa_err) < 0) {
            fprintf(stderr, "f87: PulseAudio read error: %s\n", pa_strerror(pa_err));
            break;
        }

        f87_audio_data_t data;
        f87_spectrum_analyze(samples, F87_AUDIO_BUF_SIZE, &data);
        data.timestamp_us = f87_time_us();

        f87_ring_write(ctx->audio_ring, &data);
    }

    pa_simple_free(pa);
    return NULL;
}

int f87_audio_thread_start(f87_anim_ctx_t *ctx)
{
    int rc = pthread_create(&ctx->audio_thread, NULL, audio_thread_func, ctx);
    if (rc != 0) {
        atomic_store(&ctx->error, F87_ERR_AUDIO);
        return F87_ERR_AUDIO;
    }
    return F87_OK;
}

#else /* !F87_HAS_AUDIO */

#include "animate_internal.h"

int f87_audio_thread_start(f87_anim_ctx_t *ctx)
{
    (void)ctx;
    return F87_ERR_AUDIO;
}

#endif /* F87_HAS_AUDIO */
