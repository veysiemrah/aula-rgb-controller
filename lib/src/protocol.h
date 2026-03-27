#ifndef F87_PROTOCOL_H
#define F87_PROTOCOL_H

#include "f87/device.h"
#include "f87/lighting.h"
#include "f87/effects.h"
#include <stdint.h>

/* USB identifiers (confirmed via RE) */
#define F87_VENDOR_ID           0x258A
#define F87_PRODUCT_ID          0x010C

/* Wireless 2.4G dongle identifiers */
#define F87_VENDOR_ID_WIRELESS  0x3554
#define F87_PRODUCT_ID_WIRELESS 0xFA09

/* Interface for lighting control (vendor-defined HID, usage page 0xFF00) */
#define F87_IFACE_NUM    1

/*
 * Protocol constants derived from OpenRGB SinowealthKeyboard10c analysis.
 * Communication uses HID Feature Reports via USB control transfers,
 * NOT interrupt transfers.
 */
#define F87_REPORT_ID        0x06
#define F87_REPORT_SIZE      520   /* all feature reports are 520 bytes */
#define F87_TIMEOUT_MS       1000

/* Command bytes (byte 1 of the 520-byte feature report) */
#define F87_CMD_QUERY_MODEL  0x82  /* query device model / info */
#define F87_CMD_SET_LEDS     0x08  /* direct per-key RGB update */

/* Model IDs returned in query response (byte 13) */
#define F87_MODEL_F87_PRO    0x0B

/* LED data layout within the SET_LEDS packet */
#define F87_LED_DATA_OFFSET  0x08  /* first RGB byte starts here */
#define F87_LED_BUFFER_KEYS  0x7A  /* 122 — key count field in packet */

/* Direct mode keepalive: keyboard reverts after ~1s without updates */
#define F87_KEEPALIVE_MS     500

/* HID control transfer constants */
#define F87_HID_SET_REPORT   0x09
#define F87_HID_GET_REPORT   0x01
#define F87_HID_RT_OUT       0x21  /* host-to-device, class, interface */
#define F87_HID_RT_IN        0xA1  /* device-to-host, class, interface */
/* wValue = 0x0300 | report_id (feature report type = 0x03) */
#define F87_HID_WVALUE       (0x0300 | F87_REPORT_ID)

typedef struct {
    uint8_t data[F87_REPORT_SIZE];
} f87_packet;

/* Internal device structure */
struct f87_ctx {
    void *usb_ctx;   /* libusb_context* */
};

struct f87_device {
    f87_ctx *ctx;
    void *usb_handle;        /* libusb_device_handle* */
    f87_device_info info;
    char firmware_ver[32];
    int iface_claimed;
    uint8_t model_id;        /* device model from query response */

    /* Per-key color buffer for batch apply */
    f87_color key_colors[128];
    int key_dirty[128];
    int num_keys;
};

/* Packet building */
void f87_pkt_init(f87_packet *pkt);
void f87_pkt_build_query_model(f87_packet *pkt);
void f87_pkt_build_direct_leds(f87_packet *pkt, const f87_color *colors,
                                const uint8_t *led_indices, int num_keys);

/* Packet send/recv via HID feature reports (control transfers) */
int  f87_pkt_send(f87_device *dev, const f87_packet *pkt);
int  f87_pkt_recv(f87_device *dev, f87_packet *pkt, int timeout_ms);

/* Key layout data (88-key TKL, includes ISO key K88) */
#define F87_KEY_COUNT 88
extern const f87_key_info f87_key_layout[F87_KEY_COUNT];

/* LED index mapping: key_id -> hardware LED address (from KB.ini RE) */
extern const uint8_t f87_led_index[F87_KEY_COUNT];

#endif /* F87_PROTOCOL_H */
