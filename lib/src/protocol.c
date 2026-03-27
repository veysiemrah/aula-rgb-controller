#include "protocol.h"
#include <libusb-1.0/libusb.h>
#include <string.h>

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
 * Build a direct per-key LED update feature report.
 *
 * Layout (520 bytes):
 *   Byte 0x00: 0x06  (report ID)
 *   Byte 0x01: 0x08  (command: set LEDs direct)
 *   Byte 0x04: 0x01
 *   Byte 0x06: 0x7A  (122 — LED count for buffer)
 *   Byte 0x07: 0x01
 *   Bytes 0x08+: RGB data, 3 bytes per LED
 *                led_index * 3 + 0x08 = offset for that LED's red byte
 *
 * Parameters:
 *   colors      — array of f87_color, one per key
 *   led_indices — hardware LED index for each key (from f87_led_index[])
 *   num_keys    — number of entries in colors/led_indices
 */
void f87_pkt_build_direct_leds(f87_packet *pkt, const f87_color *colors,
                                const uint8_t *led_indices, int num_keys)
{
    f87_pkt_init(pkt);
    pkt->data[0] = F87_REPORT_ID;       /* 0x06 */
    pkt->data[1] = F87_CMD_SET_LEDS;    /* 0x08 */
    pkt->data[4] = 0x01;
    pkt->data[6] = F87_LED_BUFFER_KEYS; /* 0x7A = 122 */
    pkt->data[7] = 0x01;

    for (int i = 0; i < num_keys; i++) {
        int offset = F87_LED_DATA_OFFSET + led_indices[i] * 3;
        /* Guard against buffer overflow (122 LEDs * 3 = 366 + 8 = 374 < 520) */
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

    if (rc < 0)
        return -1;

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

    if (rc < 0)
        return -1;

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
    { 27, "PRTSC",  1, 14 },   /* K28 */
    { 28, "SCRLK",  1, 15 },   /* K29 */
    { 29, "PAUSE",  1, 16 },   /* K30 */

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
    { 45, "INS",    2, 15 },   /* K46 */
    { 46, "HOME",   2, 16 },   /* K47 */
    { 47, "PGUP",   2, 17 },   /* K48 */

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
    { 61, "END",    3, 13 },   /* K62 */
    { 62, "PGDN",   3, 14 },   /* K63 */

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
    { 75, "UP",     4, 12 },   /* K76 */
    { 76, "RCTRL",  4, 13 },   /* K77 */

    /* Row 5: Bottom row */
    { 77, "LCTRL",  5,  0 },   /* K78 */
    { 78, "LWIN",   5,  1 },   /* K79 */
    { 79, "LALT",   5,  2 },   /* K80 */
    { 80, "SPACE",  5,  3 },   /* K81 */
    { 81, "RALT",   5,  4 },   /* K82 */
    { 82, "FN",     5,  5 },   /* K83 */
    { 83, "MENU",   5,  6 },   /* K84 */
    { 84, "LEFT",   5,  7 },   /* K85 */
    { 85, "DOWN",   5,  8 },   /* K86 */
    { 86, "RIGHT",  5,  9 },   /* K87 */

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
