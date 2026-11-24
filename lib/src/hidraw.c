/*
 * hidraw.c — locate the /dev/hidrawN node of a USB HID interface.
 *
 * Talking to the keyboard through hidraw (HIDIOCSFEATURE / HIDIOCGFEATURE)
 * lets the kernel usbhid driver stay bound to interface 1. That interface
 * also carries the Consumer Control collection used by the volume wheel and
 * media keys, so detaching the driver (as the libusb transport must) makes
 * the wheel stop working while the daemon is running.
 */
#include "hidraw.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Read a single integer from a sysfs attribute file. */
static int read_int_attr(const char *dir, const char *name, int base, long *out)
{
    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path))
        return -1;

    FILE *f = fopen(path, "r");
    if (!f)
        return -1;

    char buf[32];
    char *ok = fgets(buf, sizeof(buf), f);
    fclose(f);
    if (!ok)
        return -1;

    char *end = NULL;
    long v = strtol(buf, &end, base);
    if (end == buf)
        return -1;
    *out = v;
    return 0;
}

/* Strip the last path component in place. Returns -1 if there is none. */
static int parent_dir(char *path)
{
    char *slash = strrchr(path, '/');
    if (!slash || slash == path)
        return -1;
    *slash = '\0';
    return 0;
}

int f87_hidraw_find(const char *sysfs_root, int bus, int address, int iface,
                    char *out, size_t outsz)
{
    if (!sysfs_root || !out || outsz == 0)
        return -1;

    DIR *d = opendir(sysfs_root);
    if (!d)
        return -1;

    int found = -1;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strncmp(e->d_name, "hidraw", 6) != 0)
            continue;

        /*
         * <root>/hidrawN/device -> .../<usbdev>/<usbdev>:C.I/<hid-id>
         * The HID device's parent is the USB interface (bInterfaceNumber),
         * and its parent is the USB device (busnum, devnum).
         */
        char link[PATH_MAX];
        if (snprintf(link, sizeof(link), "%s/%s/device", sysfs_root,
                     e->d_name) >= (int)sizeof(link))
            continue;

        char path[PATH_MAX];
        if (!realpath(link, path))
            continue;

        long ifnum, busnum, devnum;
        if (parent_dir(path) < 0 ||
            read_int_attr(path, "bInterfaceNumber", 16, &ifnum) < 0)
            continue;
        if (parent_dir(path) < 0 ||
            read_int_attr(path, "busnum", 10, &busnum) < 0 ||
            read_int_attr(path, "devnum", 10, &devnum) < 0)
            continue;

        if (ifnum != iface || busnum != bus || devnum != address)
            continue;

        if (snprintf(out, outsz, "/dev/%s", e->d_name) < (int)outsz)
            found = 0;
        break;
    }

    closedir(d);
    return found;
}
