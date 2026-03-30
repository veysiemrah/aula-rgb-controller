/*
 * psd_test.c — Test protocol with Psd authentication bytes
 *
 * The keyboard's Psd is 3,0,0,0,3,66 (from KB.ini).
 * Hypothesis: write commands need Psd authentication in the packet.
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
}

static int get_settings(int fd)
{
    unsigned char q[REPORT_SIZE] = {0};
    q[0] = 0x06; q[1] = 0x87;
    ioctl(fd, HIDIOCSFEATURE(REPORT_SIZE), q);
    unsigned char r[REPORT_SIZE] = {0};
    r[0] = 0x06;
    int rc = ioctl(fd, HIDIOCGFEATURE(REPORT_SIZE), r);
    if (rc > 8) {
        printf("  Settings: brightness=%d effect=%d\n", r[8], r[9]);
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

    printf("Current:\n");
    get_settings(fd);

    /* Psd bytes from KB.ini: 3,0,0,0,3,66 (= 0x03,0x00,0x00,0x00,0x03,0x66) */
    unsigned char psd[6] = {0x03, 0x00, 0x00, 0x00, 0x03, 0x66};

    /* Test 1: Command 0x07 with Psd in bytes 2-7, then brightness/effect at 8-9 */
    printf("\nTest 1: cmd=0x07, psd@2-7, brightness=50 effect=1\n");
    {
        unsigned char cmd[REPORT_SIZE] = {0};
        cmd[0] = 0x06; cmd[1] = 0x07;
        memcpy(&cmd[2], psd, 6);
        cmd[8] = 50;  /* brightness */
        cmd[9] = 1;   /* effect: static */
        ioctl(fd, HIDIOCSFEATURE(REPORT_SIZE), cmd);
        usleep(100000);
        get_settings(fd);
    }

    /* Test 2: Command 0x07 with Psd in bytes 8-13 (where they appear in response) */
    printf("\nTest 2: cmd=0x07, psd@8-13, brightness=50@14 effect=1@15\n");
    {
        unsigned char cmd[REPORT_SIZE] = {0};
        cmd[0] = 0x06; cmd[1] = 0x07;
        memcpy(&cmd[8], psd, 6);
        cmd[14] = 50;
        cmd[15] = 1;
        ioctl(fd, HIDIOCSFEATURE(REPORT_SIZE), cmd);
        usleep(100000);
        get_settings(fd);
    }

    /* Test 3: LED command 0x08 with Psd */
    printf("\nTest 3: LED cmd=0x08 with psd@2-7\n");
    {
        unsigned char cmd[REPORT_SIZE] = {0};
        cmd[0] = 0x06; cmd[1] = 0x08;
        memcpy(&cmd[2], psd, 6);
        cmd[8] = 0x01;   /* sub-command? */
        cmd[9] = 0x7A;   /* 122 LEDs */
        cmd[10] = 0x01;
        /* Red on first few LEDs */
        for (int i = 0; i < 10; i++) {
            int off = 11 + i * 3;
            cmd[off] = 0xFF;
        }
        ioctl(fd, HIDIOCSFEATURE(REPORT_SIZE), cmd);
        usleep(100000);
        get_settings(fd);
    }

    /* Test 4: Try writing brightness via 0x87 itself (echo back modified) */
    printf("\nTest 4: cmd=0x87 with brightness=50 @8\n");
    {
        unsigned char cmd[REPORT_SIZE] = {0};
        cmd[0] = 0x06; cmd[1] = 0x87;
        cmd[8] = 50;  /* brightness */
        cmd[9] = 1;   /* effect */
        ioctl(fd, HIDIOCSFEATURE(REPORT_SIZE), cmd);
        usleep(100000);
        get_settings(fd);
    }

    /* Test 5: Try command 0x87 with sub-command to SET brightness */
    printf("\nTest 5: cmd=0x87, byte2=0x01 (set?), brightness=50@8\n");
    {
        unsigned char cmd[REPORT_SIZE] = {0};
        cmd[0] = 0x06; cmd[1] = 0x87;
        cmd[2] = 0x01;  /* sub-command: set */
        cmd[8] = 50;
        cmd[9] = 1;
        ioctl(fd, HIDIOCSFEATURE(REPORT_SIZE), cmd);
        usleep(100000);
        get_settings(fd);
    }

    /* Test 6: The model query uses structure: 06 82 01 00 01 00 06
     * Maybe write uses: 06 cmd 01 00 01 00 [length] [data...] */
    printf("\nTest 6: cmd=0x07, structured like model query\n");
    {
        unsigned char cmd[REPORT_SIZE] = {0};
        cmd[0] = 0x06; cmd[1] = 0x07;
        cmd[2] = 0x01; cmd[4] = 0x01;
        cmd[6] = 0x02;  /* length=2? */
        cmd[7] = 50;    /* brightness */
        cmd[8] = 1;     /* effect */
        ioctl(fd, HIDIOCSFEATURE(REPORT_SIZE), cmd);
        usleep(100000);
        get_settings(fd);
    }

    /* Test 7: Try command 0x06 (effect set, some protocols use this) */
    printf("\nTest 7: cmd=0x06 with effect data\n");
    {
        unsigned char cmd[REPORT_SIZE] = {0};
        cmd[0] = 0x06; cmd[1] = 0x06;
        cmd[2] = 0x01;  /* effect mode */
        cmd[3] = 0x64;  /* brightness */
        cmd[4] = 0x02;  /* speed */
        cmd[5] = 0x00;  /* direction */
        cmd[6] = 0xFF; cmd[7] = 0x00; cmd[8] = 0x00; /* color: red */
        ioctl(fd, HIDIOCSFEATURE(REPORT_SIZE), cmd);
        usleep(100000);
        get_settings(fd);
    }

    /* Test 8: command 0x83 with Psd (maybe need to authenticate first) */
    printf("\nTest 8: Authenticate with 0x83+Psd, then set effect\n");
    {
        /* Step 1: Send Psd via some command */
        unsigned char auth[REPORT_SIZE] = {0};
        auth[0] = 0x06; auth[1] = 0x83;
        memcpy(&auth[2], psd, 6);
        ioctl(fd, HIDIOCSFEATURE(REPORT_SIZE), auth);
        usleep(50000);

        /* Step 2: Now try setting effect */
        unsigned char cmd[REPORT_SIZE] = {0};
        cmd[0] = 0x06; cmd[1] = 0x07;
        cmd[2] = 50;  /* brightness */
        cmd[3] = 1;   /* effect */
        ioctl(fd, HIDIOCSFEATURE(REPORT_SIZE), cmd);
        usleep(100000);
        get_settings(fd);
    }

    close(fd);
    return 0;
}
