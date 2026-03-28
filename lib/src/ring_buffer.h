#ifndef F87_RING_BUFFER_H
#define F87_RING_BUFFER_H

#include <stdatomic.h>
#include <stdint.h>
#include "f87/audio_types.h"

#define F87_AUDIO_RING_SIZE 8  /* Must be power of 2 */
#define F87_AUDIO_RING_MASK (F87_AUDIO_RING_SIZE - 1)

typedef struct {
    f87_audio_data_t slots[F87_AUDIO_RING_SIZE];
    atomic_uint_fast32_t write_idx;
    atomic_uint_fast32_t read_idx;
} f87_audio_ring_t;

/* Initialize ring buffer (zero all slots, reset indices). */
void f87_ring_init(f87_audio_ring_t *ring);

/* Write one audio data frame (producer — audio thread). */
void f87_ring_write(f87_audio_ring_t *ring, const f87_audio_data_t *data);

/* Read the most recent audio data (consumer — anim thread).
   Skips all intermediate frames to get latest.
   Returns 1 if data was read, 0 if ring is empty. */
int f87_ring_read_latest(f87_audio_ring_t *ring, f87_audio_data_t *out);

#endif /* F87_RING_BUFFER_H */
