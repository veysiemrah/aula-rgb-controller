#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include "../lib/src/sensor_config.h"
#include "../lib/src/sensor.h"
#include "../lib/src/protocol.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  %-40s ", #name); \
    tests_run++; \
    if (test_##name()) { tests_passed++; printf("PASS\n"); } \
    else { printf("FAIL\n"); } \
} while(0)

static int test_save_and_reload(void) {
    f87_sensor_profile_t profile = {0};
    strncpy(profile.profile_name, "test_custom", sizeof(profile.profile_name) - 1);

    f87_sensor_mapping_t *m = &profile.mappings[0];
    m->sensor_name = strdup("cpu_temp");
    m->mode = F87_SENSOR_MODE_BAR;
    m->key_ids[0] = 1; m->key_ids[1] = 2; m->key_ids[2] = 3; m->key_ids[3] = 4;
    m->key_count = 4;
    m->interval_ms = 1000;
    profile.mapping_count = 1;

    char path[] = "/tmp/f87_test_sensor_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return 0;
    close(fd);

    int rc = f87_sensor_config_save(path, &profile, f87_key_layout, F87_KEY_COUNT);
    free(m->sensor_name);
    if (rc < 0) { unlink(path); return 0; }

    f87_sensor_profile_t loaded = {0};
    rc = f87_sensor_config_load(path, &loaded, f87_key_layout, F87_KEY_COUNT);
    unlink(path);
    if (rc < 0) return 0;

    if (strcmp(loaded.profile_name, "test_custom") != 0) return 0;
    if (loaded.mapping_count != 1) return 0;
    if (strcmp(loaded.mappings[0].sensor_name, "cpu_temp") != 0) return 0;
    if (loaded.mappings[0].mode != F87_SENSOR_MODE_BAR) return 0;
    if (loaded.mappings[0].key_count != 4) return 0;
    if (loaded.mappings[0].key_ids[0] != 1) return 0;

    for (int i = 0; i < loaded.mapping_count; i++)
        free(loaded.mappings[i].sensor_name);

    return 1;
}

static int test_save_color_mode(void) {
    f87_sensor_profile_t profile = {0};
    strncpy(profile.profile_name, "test_color", sizeof(profile.profile_name) - 1);

    f87_sensor_mapping_t *m = &profile.mappings[0];
    m->sensor_name = strdup("ram_usage");
    m->mode = F87_SENSOR_MODE_COLOR;
    m->key_ids[0] = 0;
    m->key_count = 1;
    m->interval_ms = 1000;
    profile.mapping_count = 1;

    char path[] = "/tmp/f87_test_sensor2_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return 0;
    close(fd);

    int rc = f87_sensor_config_save(path, &profile, f87_key_layout, F87_KEY_COUNT);
    free(m->sensor_name);
    if (rc < 0) { unlink(path); return 0; }

    f87_sensor_profile_t loaded = {0};
    rc = f87_sensor_config_load(path, &loaded, f87_key_layout, F87_KEY_COUNT);
    unlink(path);
    if (rc < 0) return 0;

    if (loaded.mappings[0].mode != F87_SENSOR_MODE_COLOR) return 0;
    if (loaded.mappings[0].key_count != 1) return 0;

    for (int i = 0; i < loaded.mapping_count; i++)
        free(loaded.mappings[i].sensor_name);
    return 1;
}

static int test_save_multiple_mappings(void) {
    f87_sensor_profile_t profile = {0};
    strncpy(profile.profile_name, "test_multi", sizeof(profile.profile_name) - 1);

    profile.mappings[0].sensor_name = strdup("cpu_temp");
    profile.mappings[0].mode = F87_SENSOR_MODE_BAR;
    profile.mappings[0].key_ids[0] = 1; profile.mappings[0].key_ids[1] = 2;
    profile.mappings[0].key_count = 2;
    profile.mappings[0].interval_ms = 1000;

    profile.mappings[1].sensor_name = strdup("cpu_load");
    profile.mappings[1].mode = F87_SENSOR_MODE_COLOR;
    profile.mappings[1].key_ids[0] = 0;
    profile.mappings[1].key_count = 1;
    profile.mappings[1].interval_ms = 500;

    profile.mapping_count = 2;

    char path[] = "/tmp/f87_test_sensor3_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return 0;
    close(fd);

    int rc = f87_sensor_config_save(path, &profile, f87_key_layout, F87_KEY_COUNT);
    free(profile.mappings[0].sensor_name);
    free(profile.mappings[1].sensor_name);
    if (rc < 0) { unlink(path); return 0; }

    f87_sensor_profile_t loaded = {0};
    rc = f87_sensor_config_load(path, &loaded, f87_key_layout, F87_KEY_COUNT);
    unlink(path);
    if (rc < 0) return 0;

    if (loaded.mapping_count != 2) return 0;
    if (strcmp(loaded.mappings[0].sensor_name, "cpu_temp") != 0) return 0;
    if (strcmp(loaded.mappings[1].sensor_name, "cpu_load") != 0) return 0;
    if (loaded.mappings[1].interval_ms != 500) return 0;

    for (int i = 0; i < loaded.mapping_count; i++)
        free(loaded.mappings[i].sensor_name);
    return 1;
}

int main(void) {
    printf("Sensor config tests:\n");
    TEST(save_and_reload);
    TEST(save_color_mode);
    TEST(save_multiple_mappings);
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
