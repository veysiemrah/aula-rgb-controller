#ifndef F87_HIDRAW_H
#define F87_HIDRAW_H

#include <stddef.h>

/*
 * Find the hidraw node for USB interface `iface` of the device at
 * bus/address by walking `sysfs_root` (normally "/sys/class/hidraw").
 * On success writes "/dev/hidrawN" to `out` and returns 0; returns -1
 * if no matching node exists.
 */
int f87_hidraw_find(const char *sysfs_root, int bus, int address, int iface,
                    char *out, size_t outsz);

#endif /* F87_HIDRAW_H */
