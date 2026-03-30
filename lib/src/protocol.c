#include "protocol.h"
#include "f87/logger.h"
#include <libusb-1.0/libusb.h>
#include <string.h>
#include <unistd.h>  /* usleep */

/* F87_CMD_DELAY_US defined in protocol.h */

void f87_pkt_init(f87_packet *pkt)
{
    if (pkt)
        memset(pkt, 0, sizeof(f87_packet));
}

/*
 * Build a model-query feature report.
 * Send: {0x06, 0x82, 0x01, 0x00, 0x01, 0x00, 0x06} padded to 520 bytes.
 * Response byte 13 contains the model ID (0x0B = F87 Pro).
 */
void f87_pkt_build_query_model(f87_packet *pkt)
{
    f87_pkt_init(pkt);
    pkt->data[0] = F87_REPORT_ID;       /* 0x06 */
    pkt->data[1] = F87_CMD_QUERY_MODEL; /* 0x82 */
    pkt->data[2] = 0x01;
    pkt->data[3] = 0x00;
    pkt->data[4] = 0x01;
    pkt->data[5] = 0x00;
    pkt->data[6] = 0x06;
}

/*
 * Build a planar per-key LED feature report (cmd 0x06).
 *
 * Layout (520 bytes, planar RGB — NOT interleaved):
 *   Byte 0x00: 0x06  (report ID)
 *   Byte 0x01: 0x06  (command: per-key planar RGB)
 *   Byte 0x04: 0x01
 *   Byte 0x06: 0x7A  (122 — LED count)
 *   Byte 0x07: 0x01
 *   Bytes   8-133:  R channel (126 bytes: 122 LEDs + 4 padding)
 *   Bytes 134-259:  G channel (126 bytes)
 *   Bytes 260-385:  B channel (126 bytes)
 */
void f87_pkt_build_led_planar(f87_packet *pkt, const f87_color *colors,
                               const uint8_t *led_indices, int num_keys)
{
    f87_pkt_init(pkt);
    pkt->data[0] = F87_REPORT_ID;
    pkt->data[1] = F87_CMD_LED_PLANAR;  /* 0x06 */
    pkt->data[4] = 0x01;
    pkt->data[6] = F87_LED_BUFFER_KEYS; /* 0x7A */
    pkt->data[7] = 0x01;

    for (int i = 0; i < num_keys; i++) {
        uint8_t idx = led_indices[i];
        if (idx >= F87_LED_BUFFER_KEYS)
            continue;
        pkt->data[F87_PLANE_R + idx] = colors[i].r;
        pkt->data[F87_PLANE_G + idx] = colors[i].g;
        pkt->data[F87_PLANE_B + idx] = colors[i].b;
    }
}


/*
 * Build a custom color profile packet (cmd 0x0A).
 *
 * Fills all LED positions with the requested color using RGB triplets.
 * The full profile (not just LED[0]) is required for breathing and
 * other effects to use single-color mode correctly.
 */
void f87_pkt_build_led_custom(f87_packet *pkt, f87_color color)
{
    f87_pkt_init(pkt);
    pkt->data[0] = F87_REPORT_ID;
    pkt->data[1] = F87_CMD_LED_CUSTOM;
    pkt->data[4] = 0x01;
    pkt->data[7] = 0x02;

    /* Fill all positions with RGB triplets */
    for (int i = 29; i + 2 < F87_CUSTOM_TERMINATOR_OFFSET; i += 3) {
        pkt->data[i]     = color.r;
        pkt->data[i + 1] = color.g;
        pkt->data[i + 2] = color.b;
    }

    pkt->data[F87_CUSTOM_TERMINATOR_OFFSET]     = 0x5A;
    pkt->data[F87_CUSTOM_TERMINATOR_OFFSET + 1] = 0xA5;
}

/*
 * Build a config read trigger packet (cmd 0x84).
 * After sending this, do a GET_REPORT to receive the 136-byte config.
 */
void f87_pkt_build_config_read(f87_packet *pkt)
{
    f87_pkt_init(pkt);
    pkt->data[0] = F87_REPORT_ID;
    pkt->data[1] = F87_CMD_CONFIG_READ;  /* 0x84 */
    pkt->data[4] = 0x01;
    pkt->data[6] = 0x80;
}

/*
 * Build a config write packet (cmd 0x04).
 * Copies the cached config data and modifies effect_id and brightness.
 * Bytes 68-133 (keyboard matrix) are preserved from the read.
 */
void f87_pkt_build_config_write(f87_packet *pkt, const uint8_t *config,
                                 int config_len, uint8_t effect_id,
                                 uint8_t brightness, uint8_t speed,
                                 uint8_t colorful)
{
    f87_pkt_init(pkt);
    pkt->data[0] = F87_REPORT_ID;
    pkt->data[1] = F87_CMD_CONFIG_WRITE; /* 0x04 */
    pkt->data[4] = 0x01;
    pkt->data[6] = 0x80;

    /* Copy config data starting at byte 8 */
    int copy_len = config_len - 8;
    if (copy_len > 0 && copy_len <= F87_REPORT_SIZE - 8)
        memcpy(&pkt->data[8], &config[8], (size_t)copy_len);

    /* Set effect mode */
    pkt->data[F87_CFG_CUSTOM_FLAG] = (effect_id == F87_MODE_CUSTOM) ? 0x01 : 0x00;
    pkt->data[F87_CFG_EFFECT_ID]   = effect_id;

    /* Set per-effect brightness and speed at offset 64 + 2*effect_id */
    if (effect_id > 0 && effect_id <= 18) {
        int param_off = F87_CFG_EFFECT_PARAM(effect_id);
        if (param_off + 1 < F87_CONFIG_RESP_SIZE) {
            pkt->data[param_off] = brightness;
            uint8_t cur = pkt->data[param_off + 1];
            uint8_t upper = (speed != 0xFF) ? (speed << 4) : (cur & 0xF0);
            uint8_t lower;
            if (colorful == 0)
                lower = 0x00;       /* single color */
            else if (colorful == 1)
                lower = 0x07;       /* colorful/random */
            else
                lower = cur & 0x0F; /* 0xFF = preserve */
            pkt->data[param_off + 1] = upper | lower;
        }
    }
}

/*
 * Read device config via the 4-step protocol (steps 2-3).
 * Sends 0x84 trigger, then GET_REPORT to read 136-byte config.
 * Result is cached in dev->config[].
 */
int f87_config_read(f87_device *dev)
{
    if (!dev)
        return -1;

    /* Step 2: Send config read trigger */
    f87_packet pkt;
    f87_pkt_build_config_read(&pkt);
    int rc = f87_pkt_send(dev, &pkt);
    if (rc < 0)
        return rc;

    usleep(F87_CMD_DELAY_US);

    /* Step 3: Read config response */
    f87_packet resp;
    rc = f87_pkt_recv(dev, &resp, F87_TIMEOUT_MS);
    if (rc < 0)
        return rc;

    /* Cache the response (typically 136 bytes) */
    int len = rc;
    if (len > F87_CONFIG_RESP_SIZE)
        len = F87_CONFIG_RESP_SIZE;
    memcpy(dev->config, resp.data, (size_t)len);
    dev->config_valid = 1;

    return 0;
}

/*
 * Write device config (step 4 of the protocol).
 * Reads config first if not cached, then writes modified version.
 */
int f87_config_write(f87_device *dev, uint8_t effect_id, uint8_t brightness,
                     uint8_t speed, uint8_t colorful)
{
    if (!dev)
        return -1;

    /* Read config if we don't have a cached copy */
    if (!dev->config_valid) {
        int rc = f87_config_read(dev);
        if (rc < 0)
            return rc;
    }

    /* Clamp brightness to hardware range */
    if (brightness < F87_BRIGHTNESS_MIN)
        brightness = F87_BRIGHTNESS_MIN;
    if (brightness > F87_BRIGHTNESS_MAX)
        brightness = F87_BRIGHTNESS_MAX;

    /* Clamp speed if specified */
    if (speed != 0xFF && speed > F87_SPEED_MAX)
        speed = F87_SPEED_MAX;

    /* Step 4: Write modified config */
    f87_packet pkt;
    f87_pkt_build_config_write(&pkt, dev->config, F87_CONFIG_RESP_SIZE,
                                effect_id, brightness, speed, colorful);
    usleep(F87_CMD_DELAY_US);
    int rc = f87_pkt_send(dev, &pkt);
    if (rc < 0)
        return rc;

    /* Update cache */
    dev->config[F87_CFG_EFFECT_ID] = effect_id;
    if (effect_id > 0 && effect_id <= 18) {
        int off = F87_CFG_EFFECT_PARAM(effect_id);
        dev->config[off] = brightness;
    }

    return 0;
}

/*
 * Send a feature report with a custom report ID.
 * Used for Report IDs 0x35-0x3C (separate from the main 0x06 protocol).
 */
int f87_pkt_send_report(f87_device *dev, uint8_t report_id,
                         const uint8_t *data, int len)
{
    if (!dev || !data || !dev->usb_handle || len <= 0)
        return -1;

    uint16_t wvalue = 0x0300 | report_id;

    int rc = libusb_control_transfer(
        (libusb_device_handle *)dev->usb_handle,
        F87_HID_RT_OUT,
        F87_HID_SET_REPORT,
        wvalue,
        F87_IFACE_NUM,
        (unsigned char *)data,
        (uint16_t)len,
        F87_TIMEOUT_MS
    );

    return (rc < 0) ? -1 : rc;
}

/*
 * Enable direct mode (CMD 0x08) via Report 0x3C.
 *
 * The AULA software sends this sequence before starting CMD 0x08:
 *   1. Report 0x39: [39 20 06 00 01 00] — param reset
 *   2. Report 0x3C: [3c 20 01 00]       — ENABLE direct mode
 *   3. Report 0x39: [39 20 06 01 01 00] — param confirm
 */
int f87_direct_mode_enable(f87_device *dev)
{
    if (!dev)
        return -1;

    /* Step 1: param reset via Report 0x39 (non-fatal — PIPE error on some FW) */
    uint8_t reset[] = {0x39, 0x20, 0x06, 0x00, 0x01, 0x00};
    f87_pkt_send_report(dev, 0x39, reset, sizeof(reset));
    usleep(F87_CMD_DELAY_US);

    /* Step 2: enable via Report 0x3C (critical) */
    uint8_t enable[] = {0x3C, 0x20, 0x01, 0x00};
    int rc = f87_pkt_send_report(dev, 0x3C, enable, sizeof(enable));
    /* 0x3C may return TIMEOUT but still works on F87 TK — treat as success */
    (void)rc;
    usleep(F87_CMD_DELAY_US);

    /* Step 3: param confirm via Report 0x39 (non-fatal) */
    uint8_t confirm[] = {0x39, 0x20, 0x06, 0x01, 0x01, 0x00};
    f87_pkt_send_report(dev, 0x39, confirm, sizeof(confirm));
    usleep(F87_CMD_DELAY_US);

    return 0;
}

/*
 * Disable direct mode — restore previous hardware effect.
 */
int f87_direct_mode_disable(f87_device *dev)
{
    if (!dev)
        return -1;

    /* Read current config and write it back to restore hardware effect */
    int rc = f87_config_read(dev);
    if (rc < 0) return rc;

    uint8_t eid = dev->config[F87_CFG_EFFECT_ID];
    uint8_t brightness = F87_BRIGHTNESS_MAX;
    if (eid > 0 && eid <= 18)
        brightness = dev->config[F87_CFG_EFFECT_PARAM(eid)];

    return f87_config_write(dev, eid, brightness, 0xFF, 0xFF);
}

/*
 * Send a direct mode LED frame (CMD 0x08, interleaved RGB).
 * Must call f87_direct_mode_enable() first!
 *
 * Layout: 520 bytes, interleaved RGB (3 bytes per LED at offset 8):
 *   led_index * 3 + 8 = offset for that LED's R byte
 */
int f87_direct_send_frame(f87_device *dev, const f87_color *colors,
                           const uint8_t *led_indices, int num_keys)
{
    if (!dev || !colors || !led_indices)
        return -1;

    f87_packet pkt;
    f87_pkt_build_direct_leds(&pkt, colors, led_indices, num_keys);
    return f87_pkt_send(dev, &pkt);
}

/* Legacy direct mode packet builder (cmd 0x08, interleaved RGB) */
void f87_pkt_build_direct_leds(f87_packet *pkt, const f87_color *colors,
                                const uint8_t *led_indices, int num_keys)
{
    f87_pkt_init(pkt);
    pkt->data[0] = F87_REPORT_ID;
    pkt->data[1] = F87_CMD_SET_LEDS;    /* 0x08 */
    pkt->data[4] = 0x01;
    pkt->data[6] = F87_LED_BUFFER_KEYS;
    pkt->data[7] = 0x01;

    for (int i = 0; i < num_keys; i++) {
        int offset = F87_LED_DATA_OFFSET + led_indices[i] * 3;
        if (offset + 2 >= F87_REPORT_SIZE)
            continue;
        pkt->data[offset]     = colors[i].r;
        pkt->data[offset + 1] = colors[i].g;
        pkt->data[offset + 2] = colors[i].b;
    }
}

/*
 * Send a feature report to the device via USB control transfer.
 *
 * HID SET_REPORT:
 *   bmRequestType = 0x21 (host-to-device, class, interface)
 *   bRequest      = 0x09 (SET_REPORT)
 *   wValue        = 0x0300 | report_id
 *   wIndex        = interface number
 */
int f87_pkt_send(f87_device *dev, const f87_packet *pkt)
{
    if (!dev || !pkt || !dev->usb_handle)
        return -1;

    int rc = libusb_control_transfer(
        (libusb_device_handle *)dev->usb_handle,
        F87_HID_RT_OUT,          /* 0x21 */
        F87_HID_SET_REPORT,      /* 0x09 */
        F87_HID_WVALUE,          /* 0x0306 */
        F87_IFACE_NUM,           /* interface 1 */
        (unsigned char *)pkt->data,
        F87_REPORT_SIZE,
        F87_TIMEOUT_MS
    );

    if (rc < 0) {
        F87_TRACE(F87_SRC_USB, "Send failed: %d", rc);
        return -1;
    }

    F87_TRACE(F87_SRC_USB, "Send: %d bytes, cmd=0x%02X", rc, pkt->data[1]);
    return rc;
}

/*
 * Receive a feature report from the device via USB control transfer.
 *
 * HID GET_REPORT:
 *   bmRequestType = 0xA1 (device-to-host, class, interface)
 *   bRequest      = 0x01 (GET_REPORT)
 *   wValue        = 0x0300 | report_id
 *   wIndex        = interface number
 */
int f87_pkt_recv(f87_device *dev, f87_packet *pkt, int timeout_ms)
{
    if (!dev || !pkt || !dev->usb_handle)
        return -1;

    f87_pkt_init(pkt);

    int rc = libusb_control_transfer(
        (libusb_device_handle *)dev->usb_handle,
        F87_HID_RT_IN,           /* 0xA1 */
        F87_HID_GET_REPORT,      /* 0x01 */
        F87_HID_WVALUE,          /* 0x0306 */
        F87_IFACE_NUM,           /* interface 1 */
        pkt->data,
        F87_REPORT_SIZE,
        timeout_ms > 0 ? timeout_ms : F87_TIMEOUT_MS
    );

    if (rc < 0) {
        F87_TRACE(F87_SRC_USB, "Recv failed: %d", rc);
        return -1;
    }

    F87_TRACE(F87_SRC_USB, "Recv: %d bytes", rc);
    return rc;
}

/*
 * Full 88-key TKL layout table (K1-K88 from KB.ini RE).
 * key_id values are sequential 0-87; row/col are physical position.
 * K88 is the ISO extra key between LSHIFT and Z.
 */
const f87_key_info f87_key_layout[F87_KEY_COUNT] = {
    /* Row 0: Function row */
    {  0, "ESC",    0,  0 },   /* K1  */
    {  1, "F1",     0,  1 },   /* K2  */
    {  2, "F2",     0,  2 },   /* K3  */
    {  3, "F3",     0,  3 },   /* K4  */
    {  4, "F4",     0,  4 },   /* K5  */
    {  5, "F5",     0,  5 },   /* K6  */
    {  6, "F6",     0,  6 },   /* K7  */
    {  7, "F7",     0,  7 },   /* K8  */
    {  8, "F8",     0,  8 },   /* K9  */
    {  9, "F9",     0,  9 },   /* K10 */
    { 10, "F10",    0, 10 },   /* K11 */
    { 11, "F11",    0, 11 },   /* K12 */
    { 12, "F12",    0, 12 },   /* K13 */

    /* Row 1: Number row */
    { 13, "GRAVE",  1,  0 },   /* K14 */
    { 14, "1",      1,  1 },   /* K15 */
    { 15, "2",      1,  2 },   /* K16 */
    { 16, "3",      1,  3 },   /* K17 */
    { 17, "4",      1,  4 },   /* K18 */
    { 18, "5",      1,  5 },   /* K19 */
    { 19, "6",      1,  6 },   /* K20 */
    { 20, "7",      1,  7 },   /* K21 */
    { 21, "8",      1,  8 },   /* K22 */
    { 22, "9",      1,  9 },   /* K23 */
    { 23, "0",      1, 10 },   /* K24 */
    { 24, "MINUS",  1, 11 },   /* K25 */
    { 25, "EQUAL",  1, 12 },   /* K26 */
    { 26, "BKSP",   1, 13 },   /* K27 */
    { 27, "PRTSC",  0, 14 },   /* K28 — physically row 0 (F key row) */
    { 28, "SCRLK",  0, 15 },   /* K29 — physically row 0 */
    { 29, "PAUSE",  0, 16 },   /* K30 — physically row 0 */

    /* Row 2: QWERTY row */
    { 30, "TAB",    2,  0 },   /* K31 */
    { 31, "Q",      2,  1 },   /* K32 */
    { 32, "W",      2,  2 },   /* K33 */
    { 33, "E",      2,  3 },   /* K34 */
    { 34, "R",      2,  4 },   /* K35 */
    { 35, "T",      2,  5 },   /* K36 */
    { 36, "Y",      2,  6 },   /* K37 */
    { 37, "U",      2,  7 },   /* K38 */
    { 38, "I",      2,  8 },   /* K39 */
    { 39, "O",      2,  9 },   /* K40 */
    { 40, "P",      2, 10 },   /* K41 */
    { 41, "LBRKT",  2, 11 },   /* K42 */
    { 42, "RBRKT",  2, 12 },   /* K43 */
    { 43, "ENTER",  2, 13 },   /* K44 */
    { 44, "DEL",    2, 14 },   /* K45 */
    { 45, "INS",    1, 14 },   /* K46 — physically row 1 (number row) */
    { 46, "HOME",   1, 15 },   /* K47 — physically row 1 */
    { 47, "PGUP",   1, 16 },   /* K48 — physically row 1 */

    /* Row 3: Home row */
    { 48, "CAPS",   3,  0 },   /* K49 */
    { 49, "A",      3,  1 },   /* K50 */
    { 50, "S",      3,  2 },   /* K51 */
    { 51, "D",      3,  3 },   /* K52 */
    { 52, "F",      3,  4 },   /* K53 */
    { 53, "G",      3,  5 },   /* K54 */
    { 54, "H",      3,  6 },   /* K55 */
    { 55, "J",      3,  7 },   /* K56 */
    { 56, "K",      3,  8 },   /* K57 */
    { 57, "L",      3,  9 },   /* K58 */
    { 58, "SEMI",   3, 10 },   /* K59 */
    { 59, "QUOTE",  3, 11 },   /* K60 */
    { 60, "BSLSH",  3, 12 },   /* K61 */
    { 61, "END",    2, 15 },   /* K62 — physically row 2 (QWERTY row) */
    { 62, "PGDN",   2, 16 },   /* K63 — physically row 2 */

    /* Row 4: Shift row */
    { 63, "LSHFT",  4,  0 },   /* K64 */
    { 64, "Z",      4,  1 },   /* K65 */
    { 65, "X",      4,  2 },   /* K66 */
    { 66, "C",      4,  3 },   /* K67 */
    { 67, "V",      4,  4 },   /* K68 */
    { 68, "B",      4,  5 },   /* K69 */
    { 69, "N",      4,  6 },   /* K70 */
    { 70, "M",      4,  7 },   /* K71 */
    { 71, "COMMA",  4,  8 },   /* K72 */
    { 72, "DOT",    4,  9 },   /* K73 */
    { 73, "SLASH",  4, 10 },   /* K74 */
    { 74, "RSHFT",  4, 11 },   /* K75 */
    { 75, "UP",     4, 14 },   /* K76 */
    { 76, "RCTRL",  5, 14 },   /* K77 — physically row 5 (bottom row) */

    /* Row 5: Bottom row */
    { 77, "LCTRL",  5,  0 },   /* K78 */
    { 78, "LWIN",   5,  1 },   /* K79 */
    { 79, "LALT",   5,  2 },   /* K80 */
    { 80, "SPACE",  5,  3 },   /* K81 */
    { 81, "RALT",   5,  4 },   /* K82 */
    { 82, "FN",     5,  5 },   /* K83 */
    { 83, "MENU",   5,  6 },   /* K84 */
    { 84, "LEFT",   5, 13 },   /* K85 */
    { 85, "DOWN",   5, 14 },   /* K86 */
    { 86, "RIGHT",  5, 15 },   /* K87 */

    /* ISO extra key (between LSHIFT and Z) */
    { 87, "ISO",    4, 14 },   /* K88 */
};

/*
 * Hardware LED index mapping from KB.ini RE data.
 * f87_led_index[key_id] = hardware LED address used in per-key packets.
 * Key IDs follow f87_key_layout[] ordering (K1=0 .. K88=87).
 */
const uint8_t f87_led_index[F87_KEY_COUNT] = {
    /* K1  ESC   */   0,
    /* K2  F1    */  12,
    /* K3  F2    */  18,
    /* K4  F3    */  24,
    /* K5  F4    */  30,
    /* K6  F5    */  36,
    /* K7  F6    */  42,
    /* K8  F7    */  48,
    /* K9  F8    */  54,
    /* K10 F9    */  60,
    /* K11 F10   */  66,
    /* K12 F11   */  72,
    /* K13 F12   */  78,
    /* K14 GRAVE */   1,
    /* K15 1     */   7,
    /* K16 2     */  13,
    /* K17 3     */  19,
    /* K18 4     */  25,
    /* K19 5     */  31,
    /* K20 6     */  37,
    /* K21 7     */  43,
    /* K22 8     */  49,
    /* K23 9     */  55,
    /* K24 0     */  61,
    /* K25 -     */  67,
    /* K26 =     */  73,
    /* K27 BKSP  */  79,
    /* K28 PRTSC */  84,
    /* K29 SCRLK */  90,
    /* K30 PAUSE */  96,
    /* K31 TAB   */   2,
    /* K32 Q     */   8,
    /* K33 W     */  14,
    /* K34 E     */  20,
    /* K35 R     */  26,
    /* K36 T     */  32,
    /* K37 Y     */  38,
    /* K38 U     */  44,
    /* K39 I     */  50,
    /* K40 O     */  56,
    /* K41 P     */  62,
    /* K42 [     */  68,
    /* K43 ]     */  74,
    /* K44 ENTER */  81,
    /* K45 DEL   */  86,
    /* K46 INS   */  85,
    /* K47 HOME  */  91,
    /* K48 PGUP  */  97,
    /* K49 CAPS  */   3,
    /* K50 A     */   9,
    /* K51 S     */  15,
    /* K52 D     */  21,
    /* K53 F     */  27,
    /* K54 G     */  33,
    /* K55 H     */  39,
    /* K56 J     */  45,
    /* K57 K     */  51,
    /* K58 L     */  57,
    /* K59 ;     */  63,
    /* K60 '     */  69,
    /* K61 \     */  75,
    /* K62 END   */  92,
    /* K63 PGDN  */  98,
    /* K64 LSHFT */   4,
    /* K65 Z     */  10,
    /* K66 X     */  16,
    /* K67 C     */  22,
    /* K68 V     */  28,
    /* K69 B     */  34,
    /* K70 N     */  40,
    /* K71 M     */  46,
    /* K72 ,     */  52,
    /* K73 .     */  58,
    /* K74 /     */  64,
    /* K75 RSHFT */  82,
    /* K76 UP    */  94,
    /* K77 RCTRL */  83,
    /* K78 LCTRL */   5,
    /* K79 LWIN  */  11,
    /* K80 LALT  */  17,
    /* K81 SPACE */  35,
    /* K82 RALT  */  53,
    /* K83 FN    */  59,
    /* K84 MENU  */  65,
    /* K85 LEFT  */  89,
    /* K86 DOWN  */  95,
    /* K87 RIGHT */ 101,
    /* K88 ISO   */  76,
};
