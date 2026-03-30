/*
 * cmd_scan.c — Scan all command bytes (0x00-0xFF) to find which ones
 * produce non-empty responses from the keyboard.
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
    if (argc < 2) {
        fprintf(stderr, "Usage: %s /dev/hidrawN\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    printf("Scanning all command bytes (0x00-0xFF)...\n");
    printf("%-6s %-8s %-10s %s\n", "CMD", "SET_RC", "GET_LEN", "FIRST BYTES");
    printf("-----------------------------------------------------------\n");

    for (int cmd = 0; cmd <= 0xFF; cmd++) {
        unsigned char buf[REPORT_SIZE] = {0};
        buf[0] = 0x06;  /* report ID */
        buf[1] = (unsigned char)cmd;

        /* For query-like commands, add common sub-bytes */
        if (cmd == 0x82) {
            buf[2] = 0x01; buf[4] = 0x01; buf[6] = 0x06;
        }

        int set_rc = ioctl(fd, HIDIOCSFEATURE(REPORT_SIZE), buf);

        unsigned char resp[REPORT_SIZE] = {0};
        resp[0] = 0x06;
        int get_rc = ioctl(fd, HIDIOCGFEATURE(REPORT_SIZE), resp);

        /* Only print commands that return more than 8 bytes
           (8 = just the echoed header with no data) */
        if (get_rc > 8) {
            printf("0x%02X   %-8d %-10d ", cmd, set_rc, get_rc);
            int n = get_rc < 32 ? get_rc : 32;
            for (int i = 0; i < n; i++)
                printf("%02X ", resp[i]);
            printf("\n");
        }

        usleep(5000);  /* 5ms between commands */
    }

    printf("\nDone.\n");
    close(fd);
    return 0;
}
