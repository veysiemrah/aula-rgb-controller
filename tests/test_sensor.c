#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../lib/src/sensor.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  %-40s ", #name); \
    tests_run++; \
    if (test_##name()) { tests_passed++; printf("PASS\n"); } \
    else { printf("FAIL\n"); } \
} while(0)

static int test_registry_find(void) {
    const f87_sensor_t *s = f87_sensor_find("cpu_temp");
    return s != NULL && strcmp(s->name, "cpu_temp") == 0;
}

static int test_registry_find_all(void) {
    return f87_sensor_find("cpu_temp") != NULL &&
           f87_sensor_find("cpu_load") != NULL &&
           f87_sensor_find("ram_usage") != NULL;
}

static int test_registry_unknown(void) {
    return f87_sensor_find("nonexistent") == NULL;
}

static int test_cpu_temp_read(void) {
    const f87_sensor_t *s = f87_sensor_find("cpu_temp");
    if (!s) return 1;
    void *ctx = NULL;
    if (s->init(&ctx) < 0) return 1; /* Not available — skip */
    float val = s->read(ctx);
    s->destroy(ctx);
    return val >= 0.0f && val <= 150.0f;
}

static int test_cpu_load_read(void) {
    const f87_sensor_t *s = f87_sensor_find("cpu_load");
    if (!s) return 0;
    void *ctx = NULL;
    if (s->init(&ctx) < 0) return 0;
    s->read(ctx);
    usleep(200000);
    float val = s->read(ctx);
    s->destroy(ctx);
    return val >= 0.0f && val <= 100.0f;
}

static int test_ram_read(void) {
    const f87_sensor_t *s = f87_sensor_find("ram_usage");
    if (!s) return 0;
    void *ctx = NULL;
    if (s->init(&ctx) < 0) return 0;
    float val = s->read(ctx);
    s->destroy(ctx);
    return val >= 0.0f && val <= 100.0f;
}

static int test_normalize(void) {
    float val = f87_sensor_normalize(65.0f, 30.0f, 100.0f);
    return val > 0.49f && val < 0.51f;
}

static int test_normalize_clamp(void) {
    float below = f87_sensor_normalize(10.0f, 30.0f, 100.0f);
    float above = f87_sensor_normalize(120.0f, 30.0f, 100.0f);
    return below == 0.0f && above == 1.0f;
}

int main(void) {
    printf("Sensor tests:\n");
    TEST(registry_find);
    TEST(registry_find_all);
    TEST(registry_unknown);
    TEST(cpu_temp_read);
    TEST(cpu_load_read);
    TEST(ram_read);
    TEST(normalize);
    TEST(normalize_clamp);
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
