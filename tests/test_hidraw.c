/*
 * test_hidraw.c — f87_hidraw_find() against a fake sysfs tree.
 *
 * Layout mirrors the real kernel one:
 *   <tmp>/devices/usb1/1-1/{busnum,devnum}
 *   <tmp>/devices/usb1/1-1/1-1:1.N/bInterfaceNumber
 *   <tmp>/devices/usb1/1-1/1-1:1.N/0003:258A:010C.000X/
 *   <tmp>/class/hidraw/hidrawK/device -> ../../../devices/.../0003:...
 */
#define _DEFAULT_SOURCE
#include "hidraw.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[PATH_MAX];

static void write_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    assert(f);
    fputs(content, f);
    fclose(f);
}

static void mkdirs(const char *path)
{
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

/* Create a USB device with one HID interface and its hidraw node. */
static void add_iface(const char *usbdev, int bus, int dev, int iface,
                      const char *hidraw)
{
    char p[PATH_MAX], v[32];

    snprintf(p, sizeof(p), "%s/devices/usb%d/%s", root, bus, usbdev);
    mkdirs(p);
    snprintf(p, sizeof(p), "%s/devices/usb%d/%s/busnum", root, bus, usbdev);
    snprintf(v, sizeof(v), "%d\n", bus);
    write_file(p, v);
    snprintf(p, sizeof(p), "%s/devices/usb%d/%s/devnum", root, bus, usbdev);
    snprintf(v, sizeof(v), "%d\n", dev);
    write_file(p, v);

    snprintf(p, sizeof(p), "%s/devices/usb%d/%s/%s:1.%d/0003:258A:010C.%04X",
             root, bus, usbdev, usbdev, iface, iface + 1);
    mkdirs(p);
    snprintf(p, sizeof(p), "%s/devices/usb%d/%s/%s:1.%d/bInterfaceNumber",
             root, bus, usbdev, usbdev, iface);
    snprintf(v, sizeof(v), "%02x\n", iface);
    write_file(p, v);

    snprintf(p, sizeof(p), "%s/class/hidraw/%s", root, hidraw);
    mkdirs(p);
    char target[PATH_MAX];
    snprintf(target, sizeof(target),
             "../../../devices/usb%d/%s/%s:1.%d/0003:258A:010C.%04X",
             bus, usbdev, usbdev, iface, iface + 1);
    snprintf(p, sizeof(p), "%s/class/hidraw/%s/device", root, hidraw);
    assert(symlink(target, p) == 0);
}

static const char *class_dir(void)
{
    static char p[PATH_MAX];
    snprintf(p, sizeof(p), "%s/class/hidraw", root);
    return p;
}

static void test_finds_interface_1(void)
{
    printf("test_finds_interface_1... ");
    char out[64];
    assert(f87_hidraw_find(class_dir(), 1, 8, 1, out, sizeof(out)) == 0);
    assert(strcmp(out, "/dev/hidraw7") == 0);
    printf("PASS\n");
}

static void test_finds_interface_0(void)
{
    printf("test_finds_interface_0... ");
    char out[64];
    assert(f87_hidraw_find(class_dir(), 1, 8, 0, out, sizeof(out)) == 0);
    assert(strcmp(out, "/dev/hidraw6") == 0);
    printf("PASS\n");
}

static void test_ignores_other_devices(void)
{
    printf("test_ignores_other_devices... ");
    char out[64];
    /* Other keyboard on bus 2 */
    assert(f87_hidraw_find(class_dir(), 2, 3, 1, out, sizeof(out)) == 0);
    assert(strcmp(out, "/dev/hidraw2") == 0);
    /* No such address / interface */
    assert(f87_hidraw_find(class_dir(), 1, 9, 1, out, sizeof(out)) == -1);
    assert(f87_hidraw_find(class_dir(), 1, 8, 2, out, sizeof(out)) == -1);
    printf("PASS\n");
}

static void test_missing_root(void)
{
    printf("test_missing_root... ");
    char out[64];
    assert(f87_hidraw_find("/nonexistent/hidraw", 1, 8, 1, out,
                           sizeof(out)) == -1);
    assert(f87_hidraw_find(NULL, 1, 8, 1, out, sizeof(out)) == -1);
    printf("PASS\n");
}

static void test_small_buffer(void)
{
    printf("test_small_buffer... ");
    char out[4];
    assert(f87_hidraw_find(class_dir(), 1, 8, 1, out, sizeof(out)) == -1);
    printf("PASS\n");
}

int main(void)
{
    const char *tmp = getenv("TMPDIR");
    snprintf(root, sizeof(root), "%s/f87_hidraw_XXXXXX", tmp ? tmp : "/tmp");
    assert(mkdtemp(root));

    add_iface("1-1", 1, 8, 0, "hidraw6");
    add_iface("1-1", 1, 8, 1, "hidraw7");
    add_iface("2-4", 2, 3, 1, "hidraw2");

    test_finds_interface_1();
    test_finds_interface_0();
    test_ignores_other_devices();
    test_missing_root();
    test_small_buffer();

    char cmd[PATH_MAX + 16];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
    if (system(cmd) != 0)
        fprintf(stderr, "warning: could not remove %s\n", root);

    printf("All hidraw tests passed.\n");
    return 0;
}
