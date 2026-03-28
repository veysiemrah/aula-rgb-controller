#include <f87/f87.h>
#include <stdio.h>
#include <assert.h>

static int test_init_exit(void)
{
    printf("test_init_exit... ");
    f87_ctx *ctx = f87_init();
    assert(ctx != NULL);
    f87_exit(ctx);
    printf("PASS\n");
    return 0;
}

static int test_find_no_device(void)
{
    printf("test_find_no_device... ");
    f87_ctx *ctx = f87_init();
    f87_device_info *list = NULL;
    int count = 0;
    int rc = f87_find_devices(ctx, &list, &count);
    printf("rc=%d count=%d %s\n", rc, count,
        count > 0 ? "DEVICE FOUND!" : "no device (expected)");
    if (list) f87_free_device_list(list);
    f87_exit(ctx);
    printf("PASS\n");
    return 0;
}

static int test_version(void)
{
    printf("test_version... ");
    const char *v = f87_version_string();
    assert(v != NULL);
    printf("%s PASS\n", v);
    return 0;
}

static int test_strerror(void)
{
    printf("test_strerror... ");
    assert(f87_strerror(0) != NULL);
    assert(f87_strerror(-1) != NULL);
    assert(f87_strerror(-99) != NULL);
    printf("PASS\n");
    return 0;
}

static int test_mode_names(void)
{
    printf("test_mode_names... ");
    assert(f87_mode_name(F87_MODE_OFF) != NULL);
    assert(f87_mode_name(F87_MODE_STATIC) != NULL);
    assert(f87_mode_name(F87_MODE_BREATHING) != NULL);
    assert(f87_mode_name(F87_MODE_MARQUEE) != NULL);
    assert(f87_mode_name(F87_MODE_CUSTOM) != NULL);
    printf("PASS\n");
    return 0;
}

int main(void)
{
    printf("=== libf87 device tests ===\n");
    test_init_exit();
    test_version();
    test_strerror();
    test_mode_names();
    test_find_no_device();
    printf("=== All tests passed ===\n");
    return 0;
}
