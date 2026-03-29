#ifndef F87D_ERROR_HISTORY_H
#define F87D_ERROR_HISTORY_H

#include <f87/logger.h>
#include <pthread.h>
#include <stdint.h>

#define F87D_ERROR_RING_SIZE 128

typedef struct {
    f87_log_entry_t entries[F87D_ERROR_RING_SIZE];
    int head;
    int count;
    pthread_mutex_t lock;
} f87d_error_ring_t;

void f87d_error_ring_init(f87d_error_ring_t *ring);
void f87d_error_ring_destroy(f87d_error_ring_t *ring);

/* Insert an entry (thread-safe) */
void f87d_error_ring_push(f87d_error_ring_t *ring, const f87_log_entry_t *entry);

/* Copy up to max_out entries into out[]. Returns count. Oldest first. */
int f87d_error_ring_get_all(f87d_error_ring_t *ring,
                             f87_log_entry_t *out, int max_out);

/* Clear all entries */
void f87d_error_ring_clear(f87d_error_ring_t *ring);

/* Logger callback adapter — pass ring as userdata */
void f87d_error_ring_log_callback(const f87_log_entry_t *entry, void *userdata);

#endif
