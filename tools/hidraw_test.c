/*
 * hidraw_test.c — Test LED control via hidraw ioctl (bypassing libusb)
 *
 * Usage: ./hidraw_test /dev/hidrawN
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/hidraw.h>

#define REPORT_SIZE 520

static void hexdump(const unsigned char *buf, int len)
{
    int last_nz = 0;
    for (int i = 0; i < len; i++)
        if (buf[i]) last_nz = i;
    int n = last_nz + 1;
    if (n < 32) n = 32;
    if (n > len) n = len;
    for (int i = 0; i < n; i++) {
        printf("%02X ", buf[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    if (n < len) printf("... (%d more zero bytes)\n", len - n);
    else printf("\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s /dev/hidrawN\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    /* Get device info */
    struct hidraw_devinfo info;
    if (ioctl(fd, HIDIOCGRAWINFO, &info) == 0) {
        printf("Device: VID=%04hX PID=%04hX\n", info.vendor, info.product);
    }

    /* 1) Send model query via feature report */
    printf("\n=== Model Query ===\n");
    unsigned char query[REPORT_SIZE] = {0};
    query[0] = 0x06;  /* report ID */
    query[1] = 0x82;
    query[2] = 0x01;
    query[3] = 0x00;
    query[4] = 0x01;
    query[5] = 0x00;
    query[6] = 0x06;

    int rc = ioctl(fd, HIDIOCSFEATURE(REPORT_SIZE), query);
    printf("SET_FEATURE (model query): %d\n", rc);

    unsigned char resp[REPORT_SIZE] = {0};
    resp[0] = 0x06;  /* report ID to read */
    rc = ioctl(fd, HIDIOCGFEATURE(REPORT_SIZE), resp);
    printf("GET_FEATURE: %d bytes\n", rc);
    if (rc > 0) {
        printf("Response:\n");
        hexdump(resp, rc);
        printf("Model ID (byte 13): 0x%02X\n", resp[13]);
    }

    /* 2) Send LED command — all red */
    printf("\n=== LED Set All Red ===\n");
    unsigned char led[REPORT_SIZE] = {0};
    led[0] = 0x06;   /* report ID */
    led[1] = 0x08;   /* command: set LEDs */
    led[4] = 0x01;
    led[6] = 0x7A;   /* 122 LEDs */
    led[7] = 0x01;

    /* Fill ALL possible LED positions with red (R=FF, G=00, B=00) */
    for (int i = 0; i < 122; i++) {
        int offset = 0x08 + i * 3;
        if (offset + 2 < REPORT_SIZE) {
            led[offset]     = 0xFF;  /* R */
            led[offset + 1] = 0x00;  /* G */
            led[offset + 2] = 0x00;  /* B */
        }
    }

    rc = ioctl(fd, HIDIOCSFEATURE(REPORT_SIZE), led);
    printf("SET_FEATURE (LED red): %d\n", rc);

    if (rc < 0) {
        perror("SET_FEATURE LED");
    }

    /* Read back */
    unsigned char readback[REPORT_SIZE] = {0};
    readback[0] = 0x06;
    rc = ioctl(fd, HIDIOCGFEATURE(REPORT_SIZE), readback);
    printf("GET_FEATURE readback: %d bytes\n", rc);
    if (rc > 0) {
        printf("First 32 bytes:\n");
        hexdump(readback, rc < 64 ? rc : 64);
    }

    /* 3) Try alternate command structure — maybe mode byte differs */
    printf("\n=== Alt: command 0x0B (custom mode?) ===\n");
    unsigned char alt[REPORT_SIZE] = {0};
    alt[0] = 0x06;
    alt[1] = 0x0B;  /* possible custom mode command */
    rc = ioctl(fd, HIDIOCSFEATURE(REPORT_SIZE), alt);
    printf("SET_FEATURE (0x0B): %d\n", rc);

    printf("\n=== Alt: command 0x09 ===\n");
    alt[1] = 0x09;
    rc = ioctl(fd, HIDIOCSFEATURE(REPORT_SIZE), alt);
    printf("SET_FEATURE (0x09): %d\n", rc);

    printf("\n=== Alt: command 0x07 ===\n");
    alt[1] = 0x07;
    rc = ioctl(fd, HIDIOCSFEATURE(REPORT_SIZE), alt);
    printf("SET_FEATURE (0x07): %d\n", rc);

    close(fd);
    return 0;
}
