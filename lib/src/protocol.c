#include "protocol.h"
#include <libusb-1.0/libusb.h>
#include <string.h>

void f87_pkt_init(f87_packet *pkt)
{
    if (pkt)
        memset(pkt, 0, sizeof(f87_packet));
}

int f87_pkt_build_brightness(f87_packet *pkt, uint8_t level)
{
    if (!pkt)
        return -1;

    f87_pkt_init(pkt);
    pkt->data[0] = F87_CMD_BRIGHTNESS;
    pkt->data[1] = level;
    return 0;
}

int f87_pkt_build_effect(f87_packet *pkt, const f87_effect *effect)
{
    if (!pkt || !effect)
        return -1;

    f87_pkt_init(pkt);
    pkt->data[0]  = F87_CMD_EFFECT;
    pkt->data[1]  = (uint8_t)effect->type;
    pkt->data[2]  = effect->speed;
    pkt->data[3]  = effect->brightness;
    pkt->data[4]  = effect->color1.r;
    pkt->data[5]  = effect->color1.g;
    pkt->data[6]  = effect->color1.b;
    pkt->data[7]  = effect->color2.r;
    pkt->data[8]  = effect->color2.g;
    pkt->data[9]  = effect->color2.b;
    pkt->data[10] = (uint8_t)effect->direction;
    return 0;
}

int f87_pkt_build_per_key(f87_packet *pkt, uint8_t key_id, f87_color color)
{
    if (!pkt)
        return -1;

    f87_pkt_init(pkt);
    pkt->data[0] = F87_CMD_PER_KEY;
    pkt->data[1] = 1;  /* single key mode */
    pkt->data[2] = key_id;
    pkt->data[3] = color.r;
    pkt->data[4] = color.g;
    pkt->data[5] = color.b;
    return 0;
}

int f87_pkt_build_per_key_batch(f87_packet *pkt, const f87_color *colors,
                                 const int *dirty, int offset, int count)
{
    if (!pkt || !colors || !dirty)
        return -1;

    f87_pkt_init(pkt);
    pkt->data[0] = F87_CMD_PER_KEY;
    pkt->data[1] = 0;  /* batch mode */

    /* Maximum keys per packet: (64 - 3) / 4 = 15
     * Byte 2 = number of keys packed
     * Byte 3+ = key_id, R, G, B for each key (4 bytes per entry) */
    int max_per_pkt = (F87_PKT_SIZE - 3) / 4;
    if (max_per_pkt > 15)
        max_per_pkt = 15;

    int packed = 0;
    int pos = 3; /* start writing after header */

    for (int i = offset; i < offset + count && packed < max_per_pkt; i++) {
        if (i < 0 || i >= 128)
            break;
        if (!dirty[i])
            continue;

        pkt->data[pos++] = (uint8_t)i;       /* key_id */
        pkt->data[pos++] = colors[i].r;
        pkt->data[pos++] = colors[i].g;
        pkt->data[pos++] = colors[i].b;
        packed++;

        /* Safety: check we don't overflow the packet
         * Each entry is 4 bytes (id + RGB), we need pos + 4 <= 64 */
        if (pos + 4 > F87_PKT_SIZE)
            break;
    }

    pkt->data[2] = (uint8_t)packed;
    return packed;
}

int f87_pkt_send(f87_device *dev, const f87_packet *pkt)
{
    if (!dev || !pkt || !dev->usb_handle)
        return -1;

    int transferred = 0;
    int rc = libusb_interrupt_transfer(
        (libusb_device_handle *)dev->usb_handle,
        F87_EP_OUT,
        (unsigned char *)pkt->data,
        F87_PKT_SIZE,
        &transferred,
        F87_TIMEOUT_MS
    );

    if (rc != 0)
        return -1;

    return transferred;
}

int f87_pkt_recv(f87_device *dev, f87_packet *pkt, int timeout_ms)
{
    if (!dev || !pkt || !dev->usb_handle)
        return -1;

    int transferred = 0;
    int rc = libusb_interrupt_transfer(
        (libusb_device_handle *)dev->usb_handle,
        F87_EP_IN,
        pkt->data,
        F87_PKT_SIZE,
        &transferred,
        timeout_ms > 0 ? timeout_ms : F87_TIMEOUT_MS
    );

    if (rc != 0)
        return -1;

    return transferred;
}

/*
 * Full 87-key TKL layout table.
 * key_id values are sequential 0-86.
 */
const f87_key_info f87_key_layout[F87_KEY_COUNT] = {
    /* Row 0: Function row (16 keys) */
    {  0, "ESC",    0,  0 },
    {  1, "F1",     0,  1 },
    {  2, "F2",     0,  2 },
    {  3, "F3",     0,  3 },
    {  4, "F4",     0,  4 },
    {  5, "F5",     0,  5 },
    {  6, "F6",     0,  6 },
    {  7, "F7",     0,  7 },
    {  8, "F8",     0,  8 },
    {  9, "F9",     0,  9 },
    { 10, "F10",    0, 10 },
    { 11, "F11",    0, 11 },
    { 12, "F12",    0, 12 },
    { 13, "PRTSC",  0, 13 },
    { 14, "SCRLK",  0, 14 },
    { 15, "PAUSE",  0, 15 },

    /* Row 1: Number row (17 keys) */
    { 16, "GRAVE",  1,  0 },
    { 17, "1",      1,  1 },
    { 18, "2",      1,  2 },
    { 19, "3",      1,  3 },
    { 20, "4",      1,  4 },
    { 21, "5",      1,  5 },
    { 22, "6",      1,  6 },
    { 23, "7",      1,  7 },
    { 24, "8",      1,  8 },
    { 25, "9",      1,  9 },
    { 26, "0",      1, 10 },
    { 27, "MINUS",  1, 11 },
    { 28, "EQUAL",  1, 12 },
    { 29, "BKSP",   1, 13 },
    { 30, "INS",    1, 14 },
    { 31, "HOME",   1, 15 },
    { 32, "PGUP",   1, 16 },

    /* Row 2: QWERTY row (17 keys) */
    { 33, "TAB",    2,  0 },
    { 34, "Q",      2,  1 },
    { 35, "W",      2,  2 },
    { 36, "E",      2,  3 },
    { 37, "R",      2,  4 },
    { 38, "T",      2,  5 },
    { 39, "Y",      2,  6 },
    { 40, "U",      2,  7 },
    { 41, "I",      2,  8 },
    { 42, "O",      2,  9 },
    { 43, "P",      2, 10 },
    { 44, "LBRKT",  2, 11 },
    { 45, "RBRKT",  2, 12 },
    { 46, "BSLSH",  2, 13 },
    { 47, "DEL",    2, 14 },
    { 48, "END",    2, 15 },
    { 49, "PGDN",   2, 16 },

    /* Row 3: Home row (13 keys) */
    { 50, "CAPS",   3,  0 },
    { 51, "A",      3,  1 },
    { 52, "S",      3,  2 },
    { 53, "D",      3,  3 },
    { 54, "F",      3,  4 },
    { 55, "G",      3,  5 },
    { 56, "H",      3,  6 },
    { 57, "J",      3,  7 },
    { 58, "K",      3,  8 },
    { 59, "L",      3,  9 },
    { 60, "SEMI",   3, 10 },
    { 61, "QUOTE",  3, 11 },
    { 62, "ENTER",  3, 12 },

    /* Row 4: Shift row (13 keys) */
    { 63, "LSHFT",  4,  0 },
    { 64, "Z",      4,  1 },
    { 65, "X",      4,  2 },
    { 66, "C",      4,  3 },
    { 67, "V",      4,  4 },
    { 68, "B",      4,  5 },
    { 69, "N",      4,  6 },
    { 70, "M",      4,  7 },
    { 71, "COMMA",  4,  8 },
    { 72, "DOT",    4,  9 },
    { 73, "SLASH",  4, 10 },
    { 74, "RSHFT",  4, 11 },
    { 75, "UP",     4, 12 },

    /* Row 5: Bottom row (11 keys) */
    { 76, "LCTRL",  5,  0 },
    { 77, "LWIN",   5,  1 },
    { 78, "LALT",   5,  2 },
    { 79, "SPACE",  5,  3 },
    { 80, "RALT",   5,  4 },
    { 81, "FN",     5,  5 },
    { 82, "MENU",   5,  6 },
    { 83, "RCTRL",  5,  7 },
    { 84, "LEFT",   5,  8 },
    { 85, "DOWN",   5,  9 },
    { 86, "RIGHT",  5, 10 },
};
