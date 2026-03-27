#include "f87/f87.h"
#include "protocol.h"
#include <libusb-1.0/libusb.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Error codes */
#define F87_OK          0
#define F87_ERR_INIT   -1
#define F87_ERR_NOT_FOUND -2
#define F87_ERR_OPEN   -3
#define F87_ERR_CLAIM  -4
#define F87_ERR_IO     -5
#define F87_ERR_NOMEM  -6

const char *f87_version_string(void)
{
    return "0.1.0";
}

f87_ctx *f87_init(void)
{
    f87_ctx *ctx = calloc(1, sizeof(f87_ctx));
    if (!ctx)
        return NULL;

    libusb_context *usb_ctx = NULL;
    int rc = libusb_init(&usb_ctx);
    if (rc < 0) {
        free(ctx);
        return NULL;
    }
    ctx->usb_ctx = usb_ctx;
    return ctx;
}

void f87_exit(f87_ctx *ctx)
{
    if (!ctx)
        return;

    if (ctx->usb_ctx)
        libusb_exit((libusb_context *)ctx->usb_ctx);

    free(ctx);
}

/* Helper: read a USB string descriptor into a buffer */
static void read_string_desc(libusb_device_handle *handle, uint8_t idx,
                             char *buf, size_t bufsz)
{
    if (idx == 0 || !handle) {
        buf[0] = '\0';
        return;
    }
    int rc = libusb_get_string_descriptor_ascii(handle, idx,
                                                (unsigned char *)buf,
                                                (int)bufsz);
    if (rc < 0)
        buf[0] = '\0';
}

int f87_find_devices(f87_ctx *ctx, f87_device_info **list, int *count)
{
    if (!ctx || !list || !count)
        return F87_ERR_INIT;

    *list = NULL;
    *count = 0;

    libusb_device **devs = NULL;
    ssize_t num = libusb_get_device_list((libusb_context *)ctx->usb_ctx, &devs);
    if (num < 0)
        return F87_ERR_IO;

    /* First pass: count matching devices */
    int matched = 0;
    for (ssize_t i = 0; i < num; i++) {
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(devs[i], &desc) != 0)
            continue;
        if (desc.idVendor == F87_VENDOR_ID && desc.idProduct == F87_PRODUCT_ID)
            matched++;
    }

    if (matched == 0) {
        libusb_free_device_list(devs, 1);
        return F87_OK;
    }

    f87_device_info *infos = calloc((size_t)matched, sizeof(f87_device_info));
    if (!infos) {
        libusb_free_device_list(devs, 1);
        return F87_ERR_NOMEM;
    }

    /* Second pass: populate info structures */
    int idx = 0;
    for (ssize_t i = 0; i < num && idx < matched; i++) {
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(devs[i], &desc) != 0)
            continue;
        if (desc.idVendor != F87_VENDOR_ID || desc.idProduct != F87_PRODUCT_ID)
            continue;

        f87_device_info *info = &infos[idx];
        info->vendor_id = desc.idVendor;
        info->product_id = desc.idProduct;
        info->bus = libusb_get_bus_number(devs[i]);
        info->address = libusb_get_device_address(devs[i]);

        /* Try to open temporarily for string descriptors */
        libusb_device_handle *h = NULL;
        if (libusb_open(devs[i], &h) == 0) {
            read_string_desc(h, desc.iManufacturer,
                             info->manufacturer, sizeof(info->manufacturer));
            read_string_desc(h, desc.iProduct,
                             info->product, sizeof(info->product));
            read_string_desc(h, desc.iSerialNumber,
                             info->serial, sizeof(info->serial));
            libusb_close(h);
        } else {
            info->manufacturer[0] = '\0';
            info->product[0] = '\0';
            info->serial[0] = '\0';
        }

        idx++;
    }

    libusb_free_device_list(devs, 1);

    *list = infos;
    *count = idx;
    return F87_OK;
}

void f87_free_device_list(f87_device_info *list)
{
    free(list);
}

f87_device *f87_open(f87_ctx *ctx, const f87_device_info *info)
{
    if (!ctx || !info)
        return NULL;

    libusb_device **devs = NULL;
    ssize_t num = libusb_get_device_list((libusb_context *)ctx->usb_ctx, &devs);
    if (num < 0)
        return NULL;

    libusb_device *target = NULL;
    for (ssize_t i = 0; i < num; i++) {
        if (libusb_get_bus_number(devs[i]) == info->bus &&
            libusb_get_device_address(devs[i]) == info->address) {
            target = devs[i];
            break;
        }
    }

    if (!target) {
        libusb_free_device_list(devs, 1);
        return NULL;
    }

    libusb_device_handle *handle = NULL;
    int rc = libusb_open(target, &handle);
    libusb_free_device_list(devs, 1);
    if (rc != 0)
        return NULL;

    /* Detach kernel driver if active */
    if (libusb_kernel_driver_active(handle, F87_IFACE_NUM) == 1) {
        libusb_detach_kernel_driver(handle, F87_IFACE_NUM);
    }

    /* Claim the interface */
    rc = libusb_claim_interface(handle, F87_IFACE_NUM);
    if (rc != 0) {
        libusb_close(handle);
        return NULL;
    }

    f87_device *dev = calloc(1, sizeof(f87_device));
    if (!dev) {
        libusb_release_interface(handle, F87_IFACE_NUM);
        libusb_close(handle);
        return NULL;
    }

    dev->ctx = ctx;
    dev->usb_handle = handle;
    dev->info = *info;
    dev->iface_claimed = 1;
    dev->num_keys = F87_KEY_COUNT;
    snprintf(dev->firmware_ver, sizeof(dev->firmware_ver), "unknown");

    return dev;
}

void f87_close(f87_device *dev)
{
    if (!dev)
        return;

    libusb_device_handle *handle = (libusb_device_handle *)dev->usb_handle;

    if (handle) {
        if (dev->iface_claimed) {
            libusb_release_interface(handle, F87_IFACE_NUM);
        }
        /* Try to reattach kernel driver */
        libusb_attach_kernel_driver(handle, F87_IFACE_NUM);
        libusb_close(handle);
    }

    free(dev);
}

const char *f87_get_firmware_version(f87_device *dev)
{
    if (!dev)
        return "unknown";
    /* TODO: query firmware version from device */
    return dev->firmware_ver;
}

int f87_get_battery_level(f87_device *dev)
{
    (void)dev;
    /* TODO: query battery level from device */
    return -1;
}

const char *f87_strerror(int error)
{
    switch (error) {
    case F87_OK:        return "Success";
    case F87_ERR_INIT:  return "Initialization error";
    case F87_ERR_NOT_FOUND: return "Device not found";
    case F87_ERR_OPEN:  return "Failed to open device";
    case F87_ERR_CLAIM: return "Failed to claim interface";
    case F87_ERR_IO:    return "I/O error";
    case F87_ERR_NOMEM: return "Out of memory";
    default:            return "Unknown error";
    }
}
