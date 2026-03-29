#include "error_history.h"
#include <string.h>

void f87d_error_ring_init(f87d_error_ring_t *ring)
{
    memset(ring, 0, sizeof(*ring));
    pthread_mutex_init(&ring->lock, NULL);
}

void f87d_error_ring_destroy(f87d_error_ring_t *ring)
{
    pthread_mutex_destroy(&ring->lock);
}

void f87d_error_ring_push(f87d_error_ring_t *ring, const f87_log_entry_t *entry)
{
    pthread_mutex_lock(&ring->lock);
    memcpy(&ring->entries[ring->head], entry, sizeof(*entry));
    ring->head = (ring->head + 1) % F87D_ERROR_RING_SIZE;
    if (ring->count < F87D_ERROR_RING_SIZE)
        ring->count++;
    pthread_mutex_unlock(&ring->lock);
}

int f87d_error_ring_get_all(f87d_error_ring_t *ring,
                             f87_log_entry_t *out, int max_out)
{
    pthread_mutex_lock(&ring->lock);
    int n = ring->count < max_out ? ring->count : max_out;
    int start = (ring->head - ring->count + F87D_ERROR_RING_SIZE) % F87D_ERROR_RING_SIZE;
    for (int i = 0; i < n; i++) {
        int idx = (start + i) % F87D_ERROR_RING_SIZE;
        memcpy(&out[i], &ring->entries[idx], sizeof(f87_log_entry_t));
    }
    pthread_mutex_unlock(&ring->lock);
    return n;
}

void f87d_error_ring_clear(f87d_error_ring_t *ring)
{
    pthread_mutex_lock(&ring->lock);
    ring->head = 0;
    ring->count = 0;
    pthread_mutex_unlock(&ring->lock);
}

void f87d_error_ring_log_callback(const f87_log_entry_t *entry, void *userdata)
{
    f87d_error_ring_t *ring = userdata;
    f87d_error_ring_push(ring, entry);
}
