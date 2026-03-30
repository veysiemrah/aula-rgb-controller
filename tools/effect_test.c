/*
 * effect_test.c — Test effect/brightness control via command 0x07
 *
 * Hypothesis: 0x87 = read settings, 0x07 = write settings
 * Response format: byte 8 = brightness (0-100), byte 9 = effect ID
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/hidraw.h>

#define REPORT_SIZE 520

static void hexdump(const unsigned char *buf, int len)
{
    for (int i = 0; i < len; i++) {
        printf("%02X ", buf[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    if (len % 16 != 0) printf("\n");
}

static int get_settings(int fd)
{
    /* Send 0x87 query */
    unsigned char q[REPORT_SIZE] = {0};
    q[0] = 0x06; q[1] = 0x87;
    ioctl(fd, HIDIOCSFEATURE(REPORT_SIZE), q);

    unsigned char r[REPORT_SIZE] = {0};
    r[0] = 0x06;
    int rc = ioctl(fd, HIDIOCGFEATURE(REPORT_SIZE), r);
    if (rc > 8) {
        printf("  Settings (0x87): ");
        hexdump(r, rc);
        printf("  Brightness?: %d (0x%02X)  Effect?: %d (0x%02X)\n",
               r[8], r[8], r[9], r[9]);
    }
    return rc;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s /dev/hidrawN\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    printf("=== Current Settings ===\n");
    get_settings(fd);

    /* Test: try command 0x07 to change settings */
    /* Try different byte positions for brightness */
    printf("\n=== Test: Set brightness to 50 via 0x07 ===\n");

    /* Variant A: brightness at byte 2 */
    {
        unsigned char cmd[REPORT_SIZE] = {0};
        cmd[0] = 0x06; cmd[1] = 0x07;
        cmd[2] = 0x32;  /* 50 decimal */
        ioctl(fd, HIDIOCSFEATURE(REPORT_SIZE), cmd);
        usleep(50000);
        printf("After 0x07 [byte2=50]:\n");
        get_settings(fd);
    }

    /* Variant B: brightness at byte 8 (same position as response) */
    {
        unsigned char cmd[REPORT_SIZE] = {0};
        cmd[0] = 0x06; cmd[1] = 0x07;
        cmd[8] = 0x32;  /* 50 decimal */
        ioctl(fd, HIDIOCSFEATURE(REPORT_SIZE), cmd);
        usleep(50000);
        printf("After 0x07 [byte8=50]:\n");
        get_settings(fd);
    }

    /* Variant C: same structure as 0x82 query with sub-bytes */
    {
        unsigned char cmd[REPORT_SIZE] = {0};
        cmd[0] = 0x06; cmd[1] = 0x07;
        cmd[2] = 0x01; cmd[4] = 0x01;
        cmd[6] = 0x32;  /* brightness */
        ioctl(fd, HIDIOCSFEATURE(REPORT_SIZE), cmd);
        usleep(50000);
        printf("After 0x07 [sub-bytes, byte6=50]:\n");
        get_settings(fd);
    }

    /* Test: try to change effect via different commands */
    printf("\n=== Test: Set effect to Static (HW_ID=1) via 0x07 ===\n");
    {
        unsigned char cmd[REPORT_SIZE] = {0};
        cmd[0] = 0x06; cmd[1] = 0x07;
        cmd[2] = 0x64;  /* brightness 100 */
        cmd[3] = 0x01;  /* effect: static */
        ioctl(fd, HIDIOCSFEATURE(REPORT_SIZE), cmd);
        usleep(50000);
        printf("After set:\n");
        get_settings(fd);
    }

    /* Try command 0x02 (write counterpart of 0x82?) */
    printf("\n=== Test: Command 0x02 with effect data ===\n");
    {
        unsigned char cmd[REPORT_SIZE] = {0};
        cmd[0] = 0x06; cmd[1] = 0x02;
        cmd[2] = 0x01;  /* maybe effect ID */
        cmd[3] = 0x64;  /* maybe brightness */
        ioctl(fd, HIDIOCSFEATURE(REPORT_SIZE), cmd);
        usleep(50000);
        get_settings(fd);
    }

    /* Try setting individual LED via Report ID 5 */
    printf("\n=== Test: Report ID 5 variations ===\n");
    {
        unsigned char cmd[6] = {0x05, 0xFF, 0x00, 0x00, 0x00, 0x00};
        int rc = ioctl(fd, HIDIOCSFEATURE(6), cmd);
        printf("Report5 [FF 00 00 00 00]: %d\n", rc);
        usleep(50000);
        get_settings(fd);
    }

    /* Test: try larger 0x87 query with sub-bytes to get more data */
    printf("\n=== Test: 0x87 with sub-bytes ===\n");
    {
        unsigned char q[REPORT_SIZE] = {0};
        q[0] = 0x06; q[1] = 0x87;
        q[2] = 0x01;
        ioctl(fd, HIDIOCSFEATURE(REPORT_SIZE), q);
        unsigned char r[REPORT_SIZE] = {0};
        r[0] = 0x06;
        int rc = ioctl(fd, HIDIOCGFEATURE(REPORT_SIZE), r);
        printf("0x87 with sub[2]=01: %d bytes\n", rc);
        if (rc > 0) hexdump(r, rc > 32 ? 32 : rc);
    }
    {
        unsigned char q[REPORT_SIZE] = {0};
        q[0] = 0x06; q[1] = 0x87;
        q[2] = 0x02;
        ioctl(fd, HIDIOCSFEATURE(REPORT_SIZE), q);
        unsigned char r[REPORT_SIZE] = {0};
        r[0] = 0x06;
        int rc = ioctl(fd, HIDIOCGFEATURE(REPORT_SIZE), r);
        printf("0x87 with sub[2]=02: %d bytes\n", rc);
        if (rc > 0) hexdump(r, rc > 32 ? 32 : rc);
    }
    {
        unsigned char q[REPORT_SIZE] = {0};
        q[0] = 0x06; q[1] = 0x87;
        q[2] = 0x10;
        ioctl(fd, HIDIOCSFEATURE(REPORT_SIZE), q);
        unsigned char r[REPORT_SIZE] = {0};
        r[0] = 0x06;
        int rc = ioctl(fd, HIDIOCGFEATURE(REPORT_SIZE), r);
        printf("0x87 with sub[2]=10: %d bytes\n", rc);
        if (rc > 0) hexdump(r, rc > 32 ? 32 : rc);
    }

    close(fd);
    return 0;
}
