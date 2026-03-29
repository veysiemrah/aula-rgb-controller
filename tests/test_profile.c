#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "../daemon/src/profile_manager.h"

static void test_validate_name(void)
{
    printf("test_validate_name... ");
    assert(f87d_profile_validate_name("gaming") == 0);
    assert(f87d_profile_validate_name("my-profile") == 0);
    assert(f87d_profile_validate_name("test_123") == 0);
    assert(f87d_profile_validate_name("") < 0);
    assert(f87d_profile_validate_name(NULL) < 0);
    assert(f87d_profile_validate_name("bad name") < 0);
    assert(f87d_profile_validate_name("bad/name") < 0);
    assert(f87d_profile_validate_name("bad.name") < 0);
    printf("PASS\n");
}

static void test_json_roundtrip(void)
{
    printf("test_json_roundtrip... ");
    f87d_profile_t p = {0};
    strncpy(p.name, "test", sizeof(p.name));
    strncpy(p.category, "hw", sizeof(p.category));
    p.effect_id = 3;
    p.brightness = 4;
    p.speed = 2;
    p.colorful = 0;
    p.color[0] = 255; p.color[1] = 0; p.color[2] = 0;
    p.side_light = 1;
    p.battery_light = 0;

    char *json = f87d_profile_to_json(&p);
    assert(json != NULL);

    f87d_profile_t p2 = {0};
    int rc = f87d_profile_from_json(json, &p2);
    assert(rc == 0);
    free(json);

    assert(strcmp(p2.name, "test") == 0);
    assert(strcmp(p2.category, "hw") == 0);
    assert(p2.effect_id == 3);
    assert(p2.brightness == 4);
    assert(p2.speed == 2);
    assert(p2.colorful == 0);
    assert(p2.color[0] == 255);
    assert(p2.color[1] == 0);
    assert(p2.side_light == 1);
    assert(p2.battery_light == 0);
    assert(p2.has_per_key == false);
    printf("PASS\n");
}

static void test_json_with_per_key(void)
{
    printf("test_json_with_per_key... ");
    f87d_profile_t p = {0};
    strncpy(p.name, "custom", sizeof(p.name));
    strncpy(p.category, "hw", sizeof(p.category));
    p.effect_id = 18;
    p.brightness = 3;
    p.has_per_key = true;
    for (int i = 0; i < F87D_PROFILE_KEY_COUNT; i++) {
        p.per_key_colors[i][0] = (uint8_t)(i * 2);
        p.per_key_colors[i][1] = (uint8_t)(i * 3);
        p.per_key_colors[i][2] = (uint8_t)(i);
    }

    char *json = f87d_profile_to_json(&p);
    assert(json != NULL);

    f87d_profile_t p2 = {0};
    assert(f87d_profile_from_json(json, &p2) == 0);
    free(json);

    assert(p2.has_per_key == true);
    assert(p2.per_key_colors[10][0] == 20);
    assert(p2.per_key_colors[10][1] == 30);
    assert(p2.per_key_colors[10][2] == 10);
    printf("PASS\n");
}

static void test_json_with_sensor(void)
{
    printf("test_json_with_sensor... ");
    f87d_profile_t p = {0};
    strncpy(p.name, "sensor-test", sizeof(p.name));
    strncpy(p.category, "sensor", sizeof(p.category));
    p.effect_id = 106;
    strncpy(p.sensor_profile, "gamer", sizeof(p.sensor_profile));

    char *json = f87d_profile_to_json(&p);
    assert(json != NULL);

    f87d_profile_t p2 = {0};
    assert(f87d_profile_from_json(json, &p2) == 0);
    free(json);

    assert(strcmp(p2.sensor_profile, "gamer") == 0);
    printf("PASS\n");
}

static void test_invalid_json(void)
{
    printf("test_invalid_json... ");
    f87d_profile_t p = {0};
    assert(f87d_profile_from_json("not json", &p) < 0);
    assert(f87d_profile_from_json("{}", &p) == 0);
    printf("PASS\n");
}

int main(void)
{
    printf("=== Profile Manager Tests ===\n\n");
    test_validate_name();
    test_json_roundtrip();
    test_json_with_per_key();
    test_json_with_sensor();
    test_invalid_json();
    printf("\nAll tests passed.\n");
    return 0;
}
