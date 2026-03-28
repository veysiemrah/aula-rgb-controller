#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include "../lib/src/ring_buffer.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  %-40s ", #name); \
    tests_run++; \
    if (test_##name()) { tests_passed++; printf("PASS\n"); } \
    else { printf("FAIL\n"); } \
} while(0)

static int test_init(void)
{
    f87_audio_ring_t ring;
    f87_ring_init(&ring);
    return atomic_load(&ring.write_idx) == 0 &&
           atomic_load(&ring.read_idx) == 0;
}

static int test_write_read(void)
{
    f87_audio_ring_t ring;
    f87_ring_init(&ring);

    f87_audio_data_t data = {0};
    data.energy = 0.75f;
    data.beat = true;
    data.bands[0] = 1.0f;

    f87_ring_write(&ring, &data);

    f87_audio_data_t out;
    int got = f87_ring_read_latest(&ring, &out);
    return got == 1 && out.energy == 0.75f && out.beat == true &&
           out.bands[0] == 1.0f;
}

static int test_read_empty(void)
{
    f87_audio_ring_t ring;
    f87_ring_init(&ring);

    f87_audio_data_t out;
    int got = f87_ring_read_latest(&ring, &out);
    return got == 0;
}

static int test_overwrite(void)
{
    f87_audio_ring_t ring;
    f87_ring_init(&ring);

    for (int i = 0; i < F87_AUDIO_RING_SIZE + 4; i++) {
        f87_audio_data_t data = {0};
        data.energy = (float)i / 100.0f;
        f87_ring_write(&ring, &data);
    }

    f87_audio_data_t out;
    int got = f87_ring_read_latest(&ring, &out);
    float expected = (float)(F87_AUDIO_RING_SIZE + 3) / 100.0f;
    return got == 1 && (out.energy - expected) < 0.001f &&
           (out.energy - expected) > -0.001f;
}

static f87_audio_ring_t shared_ring;
static atomic_bool producer_done;

static void *producer_func(void *arg)
{
    (void)arg;
    for (int i = 0; i < 10000; i++) {
        f87_audio_data_t data = {0};
        data.energy = (float)i;
        data.timestamp_us = (uint64_t)i;
        f87_ring_write(&shared_ring, &data);
    }
    atomic_store(&producer_done, true);
    return NULL;
}

static int test_concurrent(void)
{
    f87_ring_init(&shared_ring);
    atomic_store(&producer_done, false);

    pthread_t producer;
    pthread_create(&producer, NULL, producer_func, NULL);

    int reads = 0;
    uint64_t last_ts = 0;
    while (!atomic_load(&producer_done) || reads == 0) {
        f87_audio_data_t out;
        if (f87_ring_read_latest(&shared_ring, &out)) {
            if (out.timestamp_us < last_ts) {
                pthread_join(producer, NULL);
                return 0;
            }
            last_ts = out.timestamp_us;
            reads++;
        }
    }

    pthread_join(producer, NULL);
    return reads > 0 && last_ts > 0;
}

int main(void)
{
    printf("Ring buffer tests:\n");
    TEST(init);
    TEST(write_read);
    TEST(read_empty);
    TEST(overwrite);
    TEST(concurrent);
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
