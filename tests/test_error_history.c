#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "../daemon/src/error_history.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  %-40s ", #name); \
    tests_run++; \
    if (test_##name()) { tests_passed++; printf("PASS\n"); } \
    else { printf("FAIL\n"); } \
} while(0)

static int test_init_empty(void)
{
    f87d_error_ring_t ring;
    f87d_error_ring_init(&ring);
    f87_log_entry_t out[1];
    int n = f87d_error_ring_get_all(&ring, out, 1);
    f87d_error_ring_destroy(&ring);
    return n == 0;
}

static int test_push_and_get(void)
{
    f87d_error_ring_t ring;
    f87d_error_ring_init(&ring);

    f87_log_entry_t e = {0};
    e.timestamp_us = 1000;
    e.level = F87_LOG_ERROR;
    e.source = F87_SRC_USB;
    e.error_code = -5;
    snprintf(e.message, sizeof(e.message), "test error");

    f87d_error_ring_push(&ring, &e);

    f87_log_entry_t out[1];
    int n = f87d_error_ring_get_all(&ring, out, 1);
    f87d_error_ring_destroy(&ring);

    return n == 1 &&
           out[0].timestamp_us == 1000 &&
           out[0].level == F87_LOG_ERROR &&
           out[0].error_code == -5 &&
           strcmp(out[0].message, "test error") == 0;
}

static int test_circular_overflow(void)
{
    f87d_error_ring_t ring;
    f87d_error_ring_init(&ring);

    for (int i = 0; i < F87D_ERROR_RING_SIZE + 10; i++) {
        f87_log_entry_t e = {0};
        e.timestamp_us = (uint64_t)i;
        e.level = F87_LOG_WARN;
        snprintf(e.message, sizeof(e.message), "msg %d", i);
        f87d_error_ring_push(&ring, &e);
    }

    f87_log_entry_t out[F87D_ERROR_RING_SIZE];
    int n = f87d_error_ring_get_all(&ring, out, F87D_ERROR_RING_SIZE);
    f87d_error_ring_destroy(&ring);

    return n == F87D_ERROR_RING_SIZE &&
           out[0].timestamp_us == 10 &&
           out[F87D_ERROR_RING_SIZE - 1].timestamp_us ==
               (uint64_t)(F87D_ERROR_RING_SIZE + 9);
}

static int test_clear(void)
{
    f87d_error_ring_t ring;
    f87d_error_ring_init(&ring);

    f87_log_entry_t e = {.level = F87_LOG_ERROR};
    f87d_error_ring_push(&ring, &e);
    f87d_error_ring_clear(&ring);

    f87_log_entry_t out[1];
    int n = f87d_error_ring_get_all(&ring, out, 1);
    f87d_error_ring_destroy(&ring);
    return n == 0;
}

static int test_log_callback_adapter(void)
{
    f87d_error_ring_t ring;
    f87d_error_ring_init(&ring);

    f87_log_entry_t e = {0};
    e.level = F87_LOG_WARN;
    e.source = F87_SRC_AUDIO;
    snprintf(e.message, sizeof(e.message), "callback test");

    f87d_error_ring_log_callback(&e, &ring);

    f87_log_entry_t out[1];
    int n = f87d_error_ring_get_all(&ring, out, 1);
    f87d_error_ring_destroy(&ring);

    return n == 1 && strcmp(out[0].message, "callback test") == 0;
}

int main(void)
{
    printf("Error history tests:\n");
    TEST(init_empty);
    TEST(push_and_get);
    TEST(circular_overflow);
    TEST(clear);
    TEST(log_callback_adapter);
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
