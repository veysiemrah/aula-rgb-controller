#ifndef F87_AUDIO_TYPES_H
#define F87_AUDIO_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#define F87_AUDIO_BANDS 6

typedef enum {
    F87_AUDIO_MONITOR = 0,  /* System audio (PulseAudio monitor source) */
    F87_AUDIO_MIC     = 1,  /* Microphone input */
} f87_audio_source_t;

typedef struct {
    float bands[F87_AUDIO_BANDS]; /* Normalized frequency bands (0.0-1.0) */
    float energy;                  /* Total audio energy (0.0-1.0) */
    float beat_intensity;          /* Beat intensity (0.0-1.0, 0=no beat) */
    bool  beat;                    /* Beat detected this frame */
    uint64_t timestamp_us;         /* Microsecond timestamp */
} f87_audio_data_t;

#endif /* F87_AUDIO_TYPES_H */
