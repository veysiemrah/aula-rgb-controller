/*
 * proto_probe.c — Systematically probe the keyboard for valid commands
 *
 * Tests:
 * 1. Report ID 5 feature report (mode control?)
 * 2. Output report via SET_REPORT (wValue=0x0206 instead of 0x0306)
 * 3. Different command bytes with Report ID 6
 * 4. Interrupt transfer on EP 2
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
    if (n < 16) n = 16;
    if (n > len) n = len;
    for (int i = 0; i < n; i++) {
        printf("%02X ", buf[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    if (n % 16 != 0) printf("\n");
    if (n < len) printf("  ... (%d more zero bytes)\n", len - n);
}

static int send_feature(int fd, unsigned char *buf, int size)
{
    return ioctl(fd, HIDIOCSFEATURE(size), buf);
}

static int get_feature(int fd, unsigned char *buf, int size)
{
    return ioctl(fd, HIDIOCGFEATURE(size), buf);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s /dev/hidrawN\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    struct hidraw_devinfo info;
    if (ioctl(fd, HIDIOCGRAWINFO, &info) == 0)
        printf("Device: VID=%04hX PID=%04hX\n\n", info.vendor, info.product);

    /* Test 1: Read Report ID 5 (small vendor feature report, 5 bytes) */
    printf("=== Test 1: GET Report ID 5 ===\n");
    unsigned char rid5[6] = {0x05};
    int rc = get_feature(fd, rid5, 6);
    printf("GET_FEATURE(ID=5): %d bytes\n", rc);
    if (rc > 0) hexdump(rid5, rc);

    /* Test 2: Write Report ID 5 with some values */
    printf("\n=== Test 2: SET Report ID 5 ===\n");
    unsigned char set5[6] = {0x05, 0x01, 0x00, 0x00, 0x00, 0x00};
    rc = send_feature(fd, set5, 6);
    printf("SET_FEATURE(ID=5, data=01 00 00 00 00): %d\n", rc);

    /* Read back */
    rid5[0] = 0x05;
    rc = get_feature(fd, rid5, 6);
    printf("GET_FEATURE(ID=5) after set: %d bytes\n", rc);
    if (rc > 0) hexdump(rid5, rc);

    /* Test 3: Try Output Report (write() on hidraw = output report) */
    printf("\n=== Test 3: Output Report (write) ===\n");
    unsigned char out_led[REPORT_SIZE] = {0};
    out_led[0] = 0x06;  /* report ID */
    out_led[1] = 0x08;
    out_led[4] = 0x01;
    out_led[6] = 0x7A;
    out_led[7] = 0x01;
    /* All red */
    for (int i = 0; i < 122; i++) {
        int off = 0x08 + i * 3;
        if (off + 2 < REPORT_SIZE) {
            out_led[off] = 0xFF;
        }
    }
    rc = write(fd, out_led, REPORT_SIZE);
    printf("write() output report: %d\n", rc);
    if (rc < 0) perror("  write");

    /* Test 4: Try smaller output report sizes */
    printf("\n=== Test 4: Output Report 64 bytes ===\n");
    rc = write(fd, out_led, 64);
    printf("write() 64 bytes: %d\n", rc);
    if (rc < 0) perror("  write");

    /* Test 5: Try command 0x83 (possibly firmware/config query) */
    printf("\n=== Test 5: Query command 0x83 ===\n");
    unsigned char q83[REPORT_SIZE] = {0};
    q83[0] = 0x06; q83[1] = 0x83;
    rc = send_feature(fd, q83, REPORT_SIZE);
    printf("SET_FEATURE(0x83): %d\n", rc);
    unsigned char r83[REPORT_SIZE] = {0};
    r83[0] = 0x06;
    rc = get_feature(fd, r83, REPORT_SIZE);
    printf("GET_FEATURE: %d bytes\n", rc);
    if (rc > 0) hexdump(r83, rc > 64 ? 64 : rc);

    /* Test 6: Try command 0x80 (possible mode/status) */
    printf("\n=== Test 6: Query command 0x80 ===\n");
    unsigned char q80[REPORT_SIZE] = {0};
    q80[0] = 0x06; q80[1] = 0x80;
    rc = send_feature(fd, q80, REPORT_SIZE);
    printf("SET_FEATURE(0x80): %d\n", rc);
    unsigned char r80[REPORT_SIZE] = {0};
    r80[0] = 0x06;
    rc = get_feature(fd, r80, REPORT_SIZE);
    printf("GET_FEATURE: %d bytes\n", rc);
    if (rc > 0) hexdump(r80, rc > 64 ? 64 : rc);

    /* Test 7: Try command 0x81 */
    printf("\n=== Test 7: Query command 0x81 ===\n");
    unsigned char q81[REPORT_SIZE] = {0};
    q81[0] = 0x06; q81[1] = 0x81;
    rc = send_feature(fd, q81, REPORT_SIZE);
    printf("SET_FEATURE(0x81): %d\n", rc);
    unsigned char r81[REPORT_SIZE] = {0};
    r81[0] = 0x06;
    rc = get_feature(fd, r81, REPORT_SIZE);
    printf("GET_FEATURE: %d bytes\n", rc);
    if (rc > 0) hexdump(r81, rc > 64 ? 64 : rc);

    /* Test 8: Read current LED state? command 0x88 */
    printf("\n=== Test 8: Query command 0x88 ===\n");
    unsigned char q88[REPORT_SIZE] = {0};
    q88[0] = 0x06; q88[1] = 0x88;
    rc = send_feature(fd, q88, REPORT_SIZE);
    printf("SET_FEATURE(0x88): %d\n", rc);
    unsigned char r88[REPORT_SIZE] = {0};
    r88[0] = 0x06;
    rc = get_feature(fd, r88, REPORT_SIZE);
    printf("GET_FEATURE: %d bytes\n", rc);
    if (rc > 0) hexdump(r88, rc > 64 ? 64 : rc);

    /* Test 9: command 0x12 (possible LED effect in some Sinowealth variants) */
    printf("\n=== Test 9: Command 0x12 ===\n");
    unsigned char q12[REPORT_SIZE] = {0};
    q12[0] = 0x06; q12[1] = 0x12;
    rc = send_feature(fd, q12, REPORT_SIZE);
    printf("SET_FEATURE(0x12): %d\n", rc);
    unsigned char r12[REPORT_SIZE] = {0};
    r12[0] = 0x06;
    rc = get_feature(fd, r12, REPORT_SIZE);
    printf("GET_FEATURE: %d bytes\n", rc);
    if (rc > 0) hexdump(r12, rc > 64 ? 64 : rc);

    close(fd);
    return 0;
}
