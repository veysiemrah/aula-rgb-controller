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

#define F87_IFACE_NUM    1
#define F87_EP_OUT       0x02
#define F87_EP_IN        0x82

#define F87_PKT_SIZE     64
#define F87_TIMEOUT_MS   1000

/* Command bytes — placeholders, to be filled from RE */
#define F87_CMD_EFFECT      0x04
#define F87_CMD_BRIGHTNESS  0x05
#define F87_CMD_PER_KEY     0x06
#define F87_CMD_QUERY       0x07

typedef struct {
    uint8_t data[F87_PKT_SIZE];
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

    /* Per-key color buffer for batch apply */
    f87_color key_colors[128];
    int key_dirty[128];
    int num_keys;
};

/* Packet building */
void f87_pkt_init(f87_packet *pkt);
int  f87_pkt_build_effect(f87_packet *pkt, const f87_effect *effect);
int  f87_pkt_build_brightness(f87_packet *pkt, uint8_t level);
int  f87_pkt_build_per_key(f87_packet *pkt, uint8_t key_id, f87_color color);
int  f87_pkt_build_per_key_batch(f87_packet *pkt, const f87_color *colors,
                                  const int *dirty, int offset, int count);

/* Packet send/recv */
int  f87_pkt_send(f87_device *dev, const f87_packet *pkt);
int  f87_pkt_recv(f87_device *dev, f87_packet *pkt, int timeout_ms);

/* Key layout data (88-key TKL, includes ISO key K88) */
#define F87_KEY_COUNT 88
extern const f87_key_info f87_key_layout[F87_KEY_COUNT];

/* LED index mapping: key_id -> hardware LED address (from KB.ini RE) */
extern const uint8_t f87_led_index[F87_KEY_COUNT];

#endif /* F87_PROTOCOL_H */
