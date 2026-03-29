#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../lib/include/f87/logger.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  %-40s ", #name); \
    tests_run++; \
    if (test_##name()) { tests_passed++; printf("PASS\n"); } \
    else { printf("FAIL\n"); } \
} while(0)

static int test_level_from_string(void)
{
    return f87_log_level_from_string("trace") == F87_LOG_TRACE &&
           f87_log_level_from_string("DEBUG") == F87_LOG_DEBUG &&
           f87_log_level_from_string("Info")  == F87_LOG_INFO &&
           f87_log_level_from_string("WARN")  == F87_LOG_WARN &&
           f87_log_level_from_string("error") == F87_LOG_ERROR &&
           f87_log_level_from_string("bogus") == -1;
}

static int test_level_to_string(void)
{
    return strcmp(f87_log_level_to_string(F87_LOG_TRACE), "TRACE") == 0 &&
           strcmp(f87_log_level_to_string(F87_LOG_DEBUG), "DEBUG") == 0 &&
           strcmp(f87_log_level_to_string(F87_LOG_INFO),  "INFO")  == 0 &&
           strcmp(f87_log_level_to_string(F87_LOG_WARN),  "WARN")  == 0 &&
           strcmp(f87_log_level_to_string(F87_LOG_ERROR), "ERROR") == 0 &&
           strcmp(f87_log_level_to_string(99), "UNKNOWN") == 0;
}

static int test_source_to_string(void)
{
    return strcmp(f87_log_source_to_string(F87_SRC_USB),    "USB")    == 0 &&
           strcmp(f87_log_source_to_string(F87_SRC_AUDIO),  "AUDIO")  == 0 &&
           strcmp(f87_log_source_to_string(F87_SRC_DBUS),   "DBUS")   == 0 &&
           strcmp(f87_log_source_to_string(F87_SRC_DEVICE), "DEVICE") == 0 &&
           strcmp(f87_log_source_to_string(F87_SRC_EFFECT), "EFFECT") == 0 &&
           strcmp(f87_log_source_to_string(F87_SRC_GUI),    "GUI")    == 0;
}

static int test_set_get_level(void)
{
    f87_log_init(F87_LOG_STDERR);
    f87_log_set_level(F87_LOG_WARN);
    int ok = f87_log_get_level() == F87_LOG_WARN;
    f87_log_set_level(F87_LOG_TRACE);
    ok = ok && f87_log_get_level() == F87_LOG_TRACE;
    f87_log_shutdown();
    return ok;
}

static f87_log_entry_t last_cb_entry;
static int cb_count;

static void test_callback(const f87_log_entry_t *entry, void *userdata)
{
    (void)userdata;
    memcpy(&last_cb_entry, entry, sizeof(*entry));
    cb_count++;
}

static int test_callback_fires_on_warn_error(void)
{
    f87_log_init(F87_LOG_STDERR);
    f87_log_set_level(F87_LOG_TRACE);
    cb_count = 0;
    memset(&last_cb_entry, 0, sizeof(last_cb_entry));
    f87_log_set_callback(test_callback, NULL);

    /* DEBUG should NOT fire callback */
    f87_log(F87_LOG_DEBUG, F87_SRC_USB, 0, "debug msg");
    int ok = cb_count == 0;

    /* WARN should fire callback */
    f87_log(F87_LOG_WARN, F87_SRC_AUDIO, -5, "warn msg %d", 42);
    ok = ok && cb_count == 1;
    ok = ok && last_cb_entry.level == F87_LOG_WARN;
    ok = ok && last_cb_entry.source == F87_SRC_AUDIO;
    ok = ok && last_cb_entry.error_code == -5;
    ok = ok && strstr(last_cb_entry.message, "warn msg 42") != NULL;

    /* ERROR should fire callback */
    f87_log(F87_LOG_ERROR, F87_SRC_DEVICE, -3, "error");
    ok = ok && cb_count == 2;
    ok = ok && last_cb_entry.level == F87_LOG_ERROR;

    f87_log_set_callback(NULL, NULL);
    f87_log_shutdown();
    return ok;
}

static int test_level_filtering(void)
{
    f87_log_init(F87_LOG_STDERR);
    f87_log_set_level(F87_LOG_ERROR);
    cb_count = 0;
    f87_log_set_callback(test_callback, NULL);

    /* WARN is below ERROR level — should be filtered */
    f87_log(F87_LOG_WARN, F87_SRC_USB, 0, "filtered");
    int ok = cb_count == 0;

    /* ERROR should fire */
    f87_log(F87_LOG_ERROR, F87_SRC_USB, 0, "not filtered");
    ok = ok && cb_count == 1;

    f87_log_set_callback(NULL, NULL);
    f87_log_shutdown();
    return ok;
}

int main(void)
{
    printf("Logger tests:\n");
    TEST(level_from_string);
    TEST(level_to_string);
    TEST(source_to_string);
    TEST(set_get_level);
    TEST(callback_fires_on_warn_error);
    TEST(level_filtering);
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
