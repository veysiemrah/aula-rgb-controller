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
 * Protocol constants confirmed via USB capture (2026-03-28).
 * Communication uses HID Feature Reports via USB control transfers.
 */
#define F87_REPORT_ID        0x06
#define F87_REPORT_SIZE      520   /* SET_REPORT size */
#define F87_CONFIG_RESP_SIZE 136   /* GET_REPORT config response size */
#define F87_TIMEOUT_MS       1000

/*
 * Command bytes (byte 1 of feature report).
 *
 * Confirmed 4-step protocol for lighting changes:
 *   1) SET_REPORT cmd 0x06 or 0x0A  (LED color data)
 *   2) SET_REPORT cmd 0x84          (trigger config read)
 *   3) GET_REPORT                   (read 136-byte config)
 *   4) SET_REPORT cmd 0x04          (write modified config)
 */
#define F87_CMD_QUERY_MODEL   0x82  /* query device model / Psd */
#define F87_CMD_LED_PLANAR    0x06  /* per-key color, planar RGB layout */
#define F87_CMD_LED_CUSTOM    0x0A  /* custom color profile (static color) */
#define F87_CMD_CONFIG_READ   0x84  /* trigger config read */
#define F87_CMD_CONFIG_WRITE  0x04  /* write modified config */
#define F87_CMD_SET_LEDS      0x08  /* direct per-key RGB (OpenRGB, unconfirmed) */

/* Model IDs returned in query response (byte 13) */
#define F87_MODEL_F87_TK     0x66
#define F87_MODEL_F87_PRO    0x0B

/* LED data layout */
#define F87_LED_DATA_OFFSET   0x08  /* first data byte in LED packets */
#define F87_LED_BUFFER_KEYS   0x7A  /* 122 — LED position count field */

/* Planar RGB channel offsets within cmd 0x06 packet */
#define F87_PLANE_SIZE        126   /* 122 LEDs + 4 padding per channel */
#define F87_PLANE_R           F87_LED_DATA_OFFSET                        /* 8   */
#define F87_PLANE_G           (F87_LED_DATA_OFFSET + F87_PLANE_SIZE)     /* 134 */
#define F87_PLANE_B           (F87_LED_DATA_OFFSET + F87_PLANE_SIZE * 2) /* 260 */

/* Custom profile (cmd 0x0A) offsets */
#define F87_CUSTOM_LED0_OFFSET 29   /* LED[0] RGB starts here (static color) */
#define F87_CUSTOM_TERMINATOR_OFFSET 514  /* 0x5A 0xA5 */

/* Config response field offsets (cmd 0x84 / 0x04) */
#define F87_CFG_CUSTOM_FLAG   17    /* 0=hw effect, 1=custom per-key */
#define F87_CFG_EFFECT_ID     18    /* keyboard effect_id byte */
#define F87_CFG_SIDE_LIGHT    26    /* side light effect (0=off,1=rainbow,2=breath_color,3=static,4=breath_red) */
#define F87_CFG_BATTERY_LIGHT 36    /* battery light effect (same values as side) */
#define F87_CFG_TERMINATOR    134   /* 0x5A 0xA5 */

/*
 * Per-effect brightness & speed: offset = 64 + 2 * effect_id
 *   Byte 0: brightness (1-4, 0=off on next press)
 *   Byte 1: (speed << 4) | flags
 *           speed upper nibble: 0=slowest, 4=fastest
 *           flags lower nibble: typically 0x7
 *
 * Example: effect_id=2 (Breathing) → offset 68: brightness, offset 69: speed
 */
#define F87_CFG_EFFECT_PARAM(id)  (64 + 2 * (id))

/* Brightness range */
#define F87_BRIGHTNESS_MIN    1
#define F87_BRIGHTNESS_MAX    4

/* Speed range (upper nibble) */
#define F87_SPEED_MIN         0
#define F87_SPEED_MAX         4

/* Direct mode keepalive: keyboard reverts after ~1s without updates */
#define F87_KEEPALIVE_MS      500

/* HID control transfer constants */
#define F87_HID_SET_REPORT    0x09
#define F87_HID_GET_REPORT    0x01
#define F87_HID_RT_OUT        0x21  /* host-to-device, class, interface */
#define F87_HID_RT_IN         0xA1  /* device-to-host, class, interface */
#define F87_HID_WVALUE        (0x0300 | F87_REPORT_ID)

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

    /* Cached config from last 0x84 read (136 bytes) */
    uint8_t config[F87_CONFIG_RESP_SIZE];
    int config_valid;
};

/* Packet building */
void f87_pkt_init(f87_packet *pkt);
void f87_pkt_build_query_model(f87_packet *pkt);

/* Planar per-key LED packet (cmd 0x06) */
void f87_pkt_build_led_planar(f87_packet *pkt, const f87_color *colors,
                               const uint8_t *led_indices, int num_keys);

/* Custom profile packet (cmd 0x0A) — sets LED[0] for static color */
void f87_pkt_build_led_custom(f87_packet *pkt, f87_color color);

/* Config read trigger (cmd 0x84) */
void f87_pkt_build_config_read(f87_packet *pkt);

/* Config write (cmd 0x04) — copies cached config, modifies fields.
 * speed = 0xFF means "don't change speed" (preserve current value). */
void f87_pkt_build_config_write(f87_packet *pkt, const uint8_t *config,
                                 int config_len, uint8_t effect_id,
                                 uint8_t brightness, uint8_t speed);

/* Legacy direct mode (cmd 0x08, OpenRGB compat) */
void f87_pkt_build_direct_leds(f87_packet *pkt, const f87_color *colors,
                                const uint8_t *led_indices, int num_keys);

/* Packet send/recv via HID feature reports (control transfers) */
int  f87_pkt_send(f87_device *dev, const f87_packet *pkt);
int  f87_pkt_recv(f87_device *dev, f87_packet *pkt, int timeout_ms);

/* Config read-modify-write helpers */
int  f87_config_read(f87_device *dev);
int  f87_config_write(f87_device *dev, uint8_t effect_id, uint8_t brightness,
                      uint8_t speed);

/* Key layout data (88-key TKL, includes ISO key K88) */
#define F87_KEY_COUNT 88
extern const f87_key_info f87_key_layout[F87_KEY_COUNT];

/* LED index mapping: key_id -> hardware LED address (from KB.ini RE) */
extern const uint8_t f87_led_index[F87_KEY_COUNT];

#endif /* F87_PROTOCOL_H */
