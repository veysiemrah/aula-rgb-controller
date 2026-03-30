/*
 * settings_check.c — Read keyboard settings after reset
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/hidraw.h>

#define REPORT_SIZE 520

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/dev/hidraw6";
    int fd = open(path, O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    /* Read settings via 0x87 */
    unsigned char q[REPORT_SIZE] = {0};
    q[0] = 0x06; q[1] = 0x87;
    ioctl(fd, HIDIOCSFEATURE(REPORT_SIZE), q);

    unsigned char r[REPORT_SIZE] = {0};
    r[0] = 0x06;
    int rc = ioctl(fd, HIDIOCGFEATURE(REPORT_SIZE), r);
    if (rc > 0) {
        printf("Settings (0x87): ");
        for (int i = 0; i < rc && i < 16; i++) printf("%02X ", r[i]);
        printf("\n");
        if (rc > 9)
            printf("Brightness: %d  Effect: %d (0x%02X)\n", r[8], r[9], r[9]);
    }

    /* Read model/Psd via 0x82 */
    unsigned char m[REPORT_SIZE] = {0};
    m[0] = 0x06; m[1] = 0x82; m[2] = 0x01; m[4] = 0x01; m[6] = 0x06;
    ioctl(fd, HIDIOCSFEATURE(REPORT_SIZE), m);

    unsigned char mr[REPORT_SIZE] = {0};
    mr[0] = 0x06;
    rc = ioctl(fd, HIDIOCGFEATURE(REPORT_SIZE), mr);
    if (rc > 0) {
        printf("Model (0x82): ");
        for (int i = 0; i < rc && i < 16; i++) printf("%02X ", mr[i]);
        printf("\n");
    }

    close(fd);
    return 0;
}
