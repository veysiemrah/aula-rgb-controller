#include "ring_buffer.h"
#include <string.h>

void f87_ring_init(f87_audio_ring_t *ring)
{
    memset(ring->slots, 0, sizeof(ring->slots));
    atomic_store(&ring->write_idx, 0);
    atomic_store(&ring->read_idx, 0);
}

void f87_ring_write(f87_audio_ring_t *ring, const f87_audio_data_t *data)
{
    uint32_t wi = atomic_load_explicit(&ring->write_idx, memory_order_relaxed);
    ring->slots[wi & F87_AUDIO_RING_MASK] = *data;
    atomic_store_explicit(&ring->write_idx, wi + 1, memory_order_release);
}

int f87_ring_read_latest(f87_audio_ring_t *ring, f87_audio_data_t *out)
{
    uint32_t wi = atomic_load_explicit(&ring->write_idx, memory_order_acquire);
    uint32_t ri = atomic_load_explicit(&ring->read_idx, memory_order_relaxed);

    if (wi == ri)
        return 0;  /* Empty */

    /* Jump to latest: read the most recently written slot */
    uint32_t latest = wi - 1;
    *out = ring->slots[latest & F87_AUDIO_RING_MASK];
    atomic_store_explicit(&ring->read_idx, wi, memory_order_relaxed);
    return 1;
}
