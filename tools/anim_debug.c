/*
 * anim_debug.c — Set ALL colors in 0x0A to blue, then breathing single
 */
#include <f87/f87.h>
#include "protocol.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static f87_device *open_dev(f87_ctx *ctx)
{
    f87_device_info *list = NULL;
    int count = 0;
    f87_find_devices(ctx, &list, &count);
    if (count == 0) return NULL;
    f87_device *dev = f87_open(ctx, &list[0]);
    f87_free_device_list(list);
    return dev;
}

int main(void)
{
    f87_ctx *ctx = f87_init();
    f87_device *dev = open_dev(ctx);
    if (!dev) { printf("No device\n"); return 1; }

    /* Build 0x0A with ALL non-zero bytes set to BLUE */
    printf("Sending 0x0A with ALL colors = BLUE...\n");
    f87_packet pkt;
    f87_pkt_init(&pkt);
    pkt.data[0] = 0x06;
    pkt.data[1] = 0x0A;
    pkt.data[4] = 0x01;
    pkt.data[7] = 0x02;

    /* Fill every possible color position with blue (R=0, G=0, B=FF) */
    /* The template has RGB triplets scattered through bytes 29-384 */
    /* Replace every 0xFF in the template with blue pattern */
    for (int i = 29; i < 512; i += 3) {
        pkt.data[i]   = 0x00;  /* R = 0 */
        pkt.data[i+1] = 0x00;  /* G = 0 */
        pkt.data[i+2] = 0xFF;  /* B = 255 */
    }

    pkt.data[514] = 0x5A;
    pkt.data[515] = 0xA5;

    f87_pkt_send(dev, &pkt);
    sleep(1);

    /* Config: breathing single */
    f87_config_read(dev);
    dev->config[18] = 0x02;
    dev->config[69] = 0x20;

    f87_packet w;
    f87_pkt_init(&w);
    w.data[0]=0x06; w.data[1]=0x04; w.data[4]=0x01; w.data[6]=0x80;
    memcpy(&w.data[8], &dev->config[8], 128);
    usleep(5000);
    f87_pkt_send(dev, &w);

    printf("All-blue breathing? Waiting...\n");
    sleep(5);

    f87_close(dev);
    f87_exit(ctx);
    printf("Done.\n");
    return 0;
}
