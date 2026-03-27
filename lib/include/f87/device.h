#ifndef F87_DEVICE_H
#define F87_DEVICE_H

#include <stdint.h>

typedef struct f87_ctx f87_ctx;
typedef struct f87_device f87_device;

typedef struct {
    uint16_t vendor_id;
    uint16_t product_id;
    char serial[64];
    char manufacturer[64];
    char product[128];
    uint8_t bus;
    uint8_t address;
} f87_device_info;

f87_ctx *f87_init(void);
void f87_exit(f87_ctx *ctx);

int f87_find_devices(f87_ctx *ctx, f87_device_info **list, int *count);
void f87_free_device_list(f87_device_info *list);

f87_device *f87_open(f87_ctx *ctx, const f87_device_info *info);
void f87_close(f87_device *dev);

const char *f87_get_firmware_version(f87_device *dev);
int f87_get_battery_level(f87_device *dev);

const char *f87_strerror(int error);

#endif
