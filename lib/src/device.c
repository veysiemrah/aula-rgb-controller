#include "f87/f87.h"
#include "protocol.h"
#include <libusb-1.0/libusb.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


const char *f87_version_string(void)
{
    return "0.2.0";
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

    /* Helper: check if VID/PID matches wired or wireless */
    #define IS_F87_WIRED(v, p) \
        ((v) == F87_VENDOR_ID && (p) == F87_PRODUCT_ID)
    #define IS_F87_WIRELESS(v, p) \
        ((v) == F87_VENDOR_ID_WIRELESS && (p) == F87_PRODUCT_ID_WIRELESS)
    #define IS_F87(v, p) (IS_F87_WIRED(v, p) || IS_F87_WIRELESS(v, p))

    /* First pass: count matching devices */
    int matched = 0;
    for (ssize_t i = 0; i < num; i++) {
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(devs[i], &desc) != 0)
            continue;
        if (IS_F87(desc.idVendor, desc.idProduct))
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
        if (!IS_F87(desc.idVendor, desc.idProduct))
            continue;

        f87_device_info *info = &infos[idx];
        info->vendor_id = desc.idVendor;
        info->product_id = desc.idProduct;
        info->is_wireless = IS_F87_WIRELESS(desc.idVendor, desc.idProduct) ? 1 : 0;
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

/*
 * Query the device model via HID feature report.
 * Returns the model ID byte, or -1 on failure.
 */
static int query_device_model(f87_device *dev)
{
    f87_packet pkt;
    f87_pkt_build_query_model(&pkt);

    int rc = f87_pkt_send(dev, &pkt);
    if (rc < 0)
        return -1;

    f87_packet resp;
    rc = f87_pkt_recv(dev, &resp, F87_TIMEOUT_MS);
    if (rc < 0)
        return -1;

    /* Model ID is at byte 13 of the response */
    return (int)resp.data[13];
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

    /* Claim interface 1 (vendor-defined HID for lighting control) */
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
    dev->config_valid = 0;
    snprintf(dev->firmware_ver, sizeof(dev->firmware_ver), "unknown");

    /*
     * Query device model to verify this is an F87 Pro (or compatible).
     * Non-fatal: if the query fails we still allow use — the VID/PID
     * match is sufficient for basic operation.
     */
    int model = query_device_model(dev);
    if (model >= 0) {
        dev->model_id = (uint8_t)model;
    }

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

    /*
     * TODO: the model query response may contain firmware version data
     * in other byte positions. For now we report the model ID if known.
     */
    if (dev->model_id != 0) {
        snprintf(dev->firmware_ver, sizeof(dev->firmware_ver),
                 "model 0x%02X", dev->model_id);
    }

    return dev->firmware_ver;
}

int f87_get_battery_level(f87_device *dev)
{
    if (!dev)
        return -1;

    /* Wired devices have no battery */
    if (!dev->info.is_wireless)
        return -1;

    /*
     * Wireless battery query protocol is not yet reverse-engineered.
     * Return -1 (unknown) until we capture the Windows software's
     * battery query sequence.
     */
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
    case -7:            return "Audio error";
    case -8:            return "Animation error";
    default:            return "Unknown error";
    }
}
