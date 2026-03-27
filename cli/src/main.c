/*
 * f87ctl — CLI for controlling Womier/Gamakay K87 (F87) keyboards
 *
 * Uses the public libf87 API for device management, lighting, and effects.
 * Includes the internal protocol header only for raw HID packet commands.
 */

#include <f87/f87.h>

/* Internal header — needed only for raw send/listen (f87_pkt_send/recv) */
#include "protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

/* ---------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------------*/

static void usage(const char *progname)
{
    fprintf(stderr,
        "Usage: %s <command> [args...]\n"
        "\n"
        "Commands:\n"
        "  list                              List connected devices\n"
        "  info                              Device information\n"
        "  brightness <0-100>                Set brightness\n"
        "  off                               Turn off lighting\n"
        "  effect <name> [options]            Set effect\n"
        "      --speed N                     Speed 0-10\n"
        "      --color RRGGBB                Primary colour\n"
        "      --direction left|right|up|down\n"
        "  key set <KEY> <RRGGBB>            Set single key colour\n"
        "  key set-all <RRGGBB>              Set all keys to colour\n"
        "  raw send \"XX XX XX ...\"            Send raw HID packet\n"
        "  raw listen                        Listen for HID packets\n"
        "\n"
        "Effect names: static, breathing, wave, rainbow, ripple, reactive\n"
        "\n", progname);
}

/**
 * Parse a hex colour string "RRGGBB" into an f87_color.
 * Returns 0 on success, -1 on failure.
 */
static int parse_color(const char *str, f87_color *out)
{
    if (!str || strlen(str) != 6)
        return -1;

    for (int i = 0; i < 6; i++) {
        if (!isxdigit((unsigned char)str[i]))
            return -1;
    }

    unsigned int r, g, b;
    if (sscanf(str, "%2x%2x%2x", &r, &g, &b) != 3)
        return -1;

    out->r = (uint8_t)r;
    out->g = (uint8_t)g;
    out->b = (uint8_t)b;
    return 0;
}

/**
 * Open the first available F87 device.
 * Caller must call f87_close() on the returned device.
 * *out_info is set to the matching device_info entry (valid until list is freed).
 * Returns NULL on failure (prints error to stderr).
 */
static f87_device *open_first_device(f87_ctx *ctx,
                                     f87_device_info **out_list,
                                     int *out_count)
{
    f87_device_info *list = NULL;
    int count = 0;

    int rc = f87_find_devices(ctx, &list, &count);
    if (rc < 0) {
        fprintf(stderr, "Error finding devices: %s\n", f87_strerror(rc));
        return NULL;
    }
    if (count == 0) {
        fprintf(stderr, "No devices found.\n");
        f87_free_device_list(list);
        return NULL;
    }

    f87_device *dev = f87_open(ctx, &list[0]);
    if (!dev) {
        fprintf(stderr, "Failed to open device.\n");
        f87_free_device_list(list);
        return NULL;
    }

    *out_list = list;
    *out_count = count;
    return dev;
}

/* ---------------------------------------------------------------------------
 * Subcommands
 * ---------------------------------------------------------------------------*/

static int cmd_list(f87_ctx *ctx)
{
    f87_device_info *list = NULL;
    int count = 0;

    int rc = f87_find_devices(ctx, &list, &count);
    if (rc < 0) {
        fprintf(stderr, "Error finding devices: %s\n", f87_strerror(rc));
        return 1;
    }
    if (count == 0) {
        printf("No devices found.\n");
        f87_free_device_list(list);
        return 0;
    }

    printf("Found %d device(s):\n\n", count);
    for (int i = 0; i < count; i++) {
        printf("  [%d] %s — %s\n"
               "      Bus %d, Address %d\n"
               "      VID:PID %04X:%04X\n\n",
               i,
               list[i].manufacturer,
               list[i].product,
               list[i].bus,
               list[i].address,
               list[i].vendor_id,
               list[i].product_id);
    }

    f87_free_device_list(list);
    return 0;
}

static int cmd_info(f87_ctx *ctx)
{
    f87_device_info *list = NULL;
    int count = 0;

    f87_device *dev = open_first_device(ctx, &list, &count);
    if (!dev)
        return 1;

    const f87_device_info *info = &list[0];

    printf("Device Information\n");
    printf("  Product:      %s\n", info->product);
    printf("  Manufacturer: %s\n", info->manufacturer);
    printf("  Serial:       %s\n", info->serial);

    const char *fw = f87_get_firmware_version(dev);
    printf("  Firmware:     %s\n", fw ? fw : "(unknown)");

    int key_count = f87_get_key_count(dev);
    printf("  Key count:    %d\n", key_count);

    printf("  VID:PID:      %04X:%04X\n", info->vendor_id, info->product_id);
    printf("  Bus:          %d\n", info->bus);
    printf("  Address:      %d\n", info->address);

    f87_close(dev);
    f87_free_device_list(list);
    return 0;
}

static int cmd_brightness(f87_ctx *ctx, int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "Usage: f87ctl brightness <0-100>\n");
        return 1;
    }

    int level = atoi(argv[0]);
    if (level < 0 || level > 100) {
        fprintf(stderr, "Brightness must be 0-100, got %d.\n", level);
        return 1;
    }

    f87_device_info *list = NULL;
    int count = 0;
    f87_device *dev = open_first_device(ctx, &list, &count);
    if (!dev)
        return 1;

    int rc = f87_set_brightness(dev, (uint8_t)level);
    if (rc < 0) {
        fprintf(stderr, "Error setting brightness: %s\n", f87_strerror(rc));
        f87_close(dev);
        f87_free_device_list(list);
        return 1;
    }

    printf("Brightness set to %d%%.\n", level);
    f87_close(dev);
    f87_free_device_list(list);
    return 0;
}

static int cmd_off(f87_ctx *ctx)
{
    f87_device_info *list = NULL;
    int count = 0;
    f87_device *dev = open_first_device(ctx, &list, &count);
    if (!dev)
        return 1;

    int rc = f87_lights_off(dev);
    if (rc < 0) {
        fprintf(stderr, "Error turning off lights: %s\n", f87_strerror(rc));
        f87_close(dev);
        f87_free_device_list(list);
        return 1;
    }

    printf("Lights off.\n");
    f87_close(dev);
    f87_free_device_list(list);
    return 0;
}

/**
 * Parse a direction string to f87_direction.
 * Returns 0 on success, -1 on failure.
 */
static int parse_direction(const char *str, f87_direction *out)
{
    if (strcasecmp(str, "right") == 0) { *out = F87_DIR_RIGHT; return 0; }
    if (strcasecmp(str, "left") == 0)  { *out = F87_DIR_LEFT;  return 0; }
    if (strcasecmp(str, "up") == 0)    { *out = F87_DIR_UP;    return 0; }
    if (strcasecmp(str, "down") == 0)  { *out = F87_DIR_DOWN;  return 0; }
    return -1;
}

static int cmd_effect(f87_ctx *ctx, int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "Usage: f87ctl effect <name> [--speed N] "
                        "[--color RRGGBB] [--direction left|right|up|down]\n");
        return 1;
    }

    const char *name = argv[0];

    /* Match effect name (case-insensitive) using f87_effect_name() */
    f87_effect_type matched = F87_EFFECT_COUNT;
    for (int i = 0; i < (int)F87_EFFECT_COUNT; i++) {
        const char *ename = f87_effect_name((f87_effect_type)i);
        if (ename && strcasecmp(ename, name) == 0) {
            matched = (f87_effect_type)i;
            break;
        }
    }

    if (matched == F87_EFFECT_COUNT) {
        fprintf(stderr, "Unknown effect '%s'.\nAvailable effects: ", name);
        for (int i = 0; i < (int)F87_EFFECT_COUNT; i++) {
            const char *ename = f87_effect_name((f87_effect_type)i);
            fprintf(stderr, "%s%s", ename ? ename : "?",
                    i < (int)F87_EFFECT_COUNT - 1 ? ", " : "\n");
        }
        return 1;
    }

    /* Defaults */
    f87_effect effect = {
        .type       = matched,
        .speed      = 5,
        .brightness = 100,
        .color1     = F87_COLOR_WHITE,
        .color2     = F87_COLOR_OFF,
        .direction  = F87_DIR_RIGHT,
    };

    /* Parse optional arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--speed") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "--speed requires a value (0-10).\n");
                return 1;
            }
            int speed = atoi(argv[i]);
            if (speed < 0 || speed > 10) {
                fprintf(stderr, "Speed must be 0-10, got %d.\n", speed);
                return 1;
            }
            effect.speed = (uint8_t)speed;
        } else if (strcmp(argv[i], "--color") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "--color requires a value (RRGGBB).\n");
                return 1;
            }
            if (parse_color(argv[i], &effect.color1) < 0) {
                fprintf(stderr, "Invalid colour '%s'. Expected RRGGBB.\n",
                        argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "--direction") == 0) {
            if (++i >= argc) {
                fprintf(stderr,
                    "--direction requires a value (left|right|up|down).\n");
                return 1;
            }
            if (parse_direction(argv[i], &effect.direction) < 0) {
                fprintf(stderr,
                    "Invalid direction '%s'. "
                    "Expected left, right, up, or down.\n", argv[i]);
                return 1;
            }
        } else {
            fprintf(stderr, "Unknown option '%s'.\n", argv[i]);
            return 1;
        }
    }

    f87_device_info *dlist = NULL;
    int dcount = 0;
    f87_device *dev = open_first_device(ctx, &dlist, &dcount);
    if (!dev)
        return 1;

    int rc = f87_set_effect(dev, &effect);
    if (rc < 0) {
        fprintf(stderr, "Error setting effect: %s\n", f87_strerror(rc));
        f87_close(dev);
        f87_free_device_list(dlist);
        return 1;
    }

    printf("Effect '%s' set (speed=%d, direction=%s).\n",
           f87_effect_name(matched), effect.speed,
           effect.direction == F87_DIR_LEFT  ? "left"  :
           effect.direction == F87_DIR_RIGHT ? "right" :
           effect.direction == F87_DIR_UP    ? "up"    : "down");

    f87_close(dev);
    f87_free_device_list(dlist);
    return 0;
}

static int cmd_key(f87_ctx *ctx, int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "Usage: f87ctl key set <KEY> <RRGGBB>\n"
                        "       f87ctl key set-all <RRGGBB>\n");
        return 1;
    }

    const char *subcmd = argv[0];

    if (strcasecmp(subcmd, "set-all") == 0) {
        if (argc < 2) {
            fprintf(stderr, "Usage: f87ctl key set-all <RRGGBB>\n");
            return 1;
        }
        f87_color color;
        if (parse_color(argv[1], &color) < 0) {
            fprintf(stderr, "Invalid colour '%s'. Expected RRGGBB.\n",
                    argv[1]);
            return 1;
        }

        f87_device_info *list = NULL;
        int count = 0;
        f87_device *dev = open_first_device(ctx, &list, &count);
        if (!dev)
            return 1;

        int rc = f87_set_all_keys(dev, color);
        if (rc < 0) {
            fprintf(stderr, "Error setting all keys: %s\n", f87_strerror(rc));
            f87_close(dev);
            f87_free_device_list(list);
            return 1;
        }

        rc = f87_apply(dev);
        if (rc < 0) {
            fprintf(stderr, "Error applying key colours: %s\n",
                    f87_strerror(rc));
            f87_close(dev);
            f87_free_device_list(list);
            return 1;
        }

        printf("All keys set to #%02X%02X%02X.\n", color.r, color.g, color.b);
        f87_close(dev);
        f87_free_device_list(list);
        return 0;

    } else if (strcasecmp(subcmd, "set") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: f87ctl key set <KEY> <RRGGBB>\n");
            return 1;
        }
        const char *key_name = argv[1];
        f87_color color;
        if (parse_color(argv[2], &color) < 0) {
            fprintf(stderr, "Invalid colour '%s'. Expected RRGGBB.\n",
                    argv[2]);
            return 1;
        }

        f87_device_info *list = NULL;
        int count = 0;
        f87_device *dev = open_first_device(ctx, &list, &count);
        if (!dev)
            return 1;

        /* Find key by name in layout */
        const f87_key_info *layout = f87_get_key_layout(dev);
        int key_count = f87_get_key_count(dev);
        int key_id = -1;

        for (int i = 0; i < key_count; i++) {
            if (layout[i].name && strcasecmp(layout[i].name, key_name) == 0) {
                key_id = layout[i].key_id;
                break;
            }
        }

        if (key_id < 0) {
            fprintf(stderr, "Unknown key '%s'. Available keys:\n", key_name);
            for (int i = 0; i < key_count; i++) {
                if (layout[i].name) {
                    fprintf(stderr, "  %s\n", layout[i].name);
                }
            }
            f87_close(dev);
            f87_free_device_list(list);
            return 1;
        }

        int rc = f87_set_key_color(dev, (uint8_t)key_id, color);
        if (rc < 0) {
            fprintf(stderr, "Error setting key colour: %s\n",
                    f87_strerror(rc));
            f87_close(dev);
            f87_free_device_list(list);
            return 1;
        }

        rc = f87_apply(dev);
        if (rc < 0) {
            fprintf(stderr, "Error applying key colours: %s\n",
                    f87_strerror(rc));
            f87_close(dev);
            f87_free_device_list(list);
            return 1;
        }

        printf("Key '%s' set to #%02X%02X%02X.\n",
               key_name, color.r, color.g, color.b);
        f87_close(dev);
        f87_free_device_list(list);
        return 0;

    } else {
        fprintf(stderr, "Unknown key subcommand '%s'.\n"
                        "Usage: f87ctl key set <KEY> <RRGGBB>\n"
                        "       f87ctl key set-all <RRGGBB>\n", subcmd);
        return 1;
    }
}

/**
 * Print a hex dump of a packet buffer.
 */
static void print_hex_dump(const uint8_t *data, int len)
{
    for (int i = 0; i < len; i++) {
        printf("%02X", data[i]);
        if (i < len - 1)
            printf(" ");
    }
    printf("\n");
}

static int cmd_raw_send(f87_ctx *ctx, int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "Usage: f87ctl raw send \"XX XX XX ...\"\n");
        return 1;
    }

    /* Join all remaining args into one string (handles both quoted and unquoted) */
    char buf[512] = {0};
    for (int i = 0; i < argc; i++) {
        if (i > 0)
            strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
        strncat(buf, argv[i], sizeof(buf) - strlen(buf) - 1);
    }

    /* Parse space-separated hex bytes */
    f87_packet pkt;
    f87_pkt_init(&pkt);

    int byte_count = 0;
    char *tok = strtok(buf, " ");
    while (tok && byte_count < F87_PKT_SIZE) {
        unsigned int val;
        if (sscanf(tok, "%x", &val) != 1 || val > 0xFF) {
            fprintf(stderr, "Invalid hex byte '%s'.\n", tok);
            return 1;
        }
        pkt.data[byte_count++] = (uint8_t)val;
        tok = strtok(NULL, " ");
    }

    if (byte_count == 0) {
        fprintf(stderr, "No bytes to send.\n");
        return 1;
    }

    printf("Sending %d byte(s):\n  ", byte_count);
    print_hex_dump(pkt.data, F87_PKT_SIZE);

    f87_device_info *list = NULL;
    int count = 0;
    f87_device *dev = open_first_device(ctx, &list, &count);
    if (!dev)
        return 1;

    int rc = f87_pkt_send(dev, &pkt);
    if (rc < 0) {
        fprintf(stderr, "Send failed: %s\n", f87_strerror(rc));
        f87_close(dev);
        f87_free_device_list(list);
        return 1;
    }
    printf("Sent.\n");

    /* Try to read a response */
    f87_packet resp;
    f87_pkt_init(&resp);
    rc = f87_pkt_recv(dev, &resp, F87_TIMEOUT_MS);
    if (rc < 0) {
        printf("No response (timeout or error).\n");
    } else {
        printf("Response:\n  ");
        print_hex_dump(resp.data, F87_PKT_SIZE);
    }

    f87_close(dev);
    f87_free_device_list(list);
    return 0;
}

static int cmd_raw_listen(f87_ctx *ctx)
{
    f87_device_info *list = NULL;
    int count = 0;
    f87_device *dev = open_first_device(ctx, &list, &count);
    if (!dev)
        return 1;

    printf("Listening for HID packets (Ctrl+C to stop)...\n\n");

    int pkt_num = 0;
    for (;;) {
        f87_packet pkt;
        f87_pkt_init(&pkt);

        int rc = f87_pkt_recv(dev, &pkt, 5000);
        if (rc < 0) {
            /* Timeout — just keep listening */
            continue;
        }

        printf("[%04d] ", ++pkt_num);
        print_hex_dump(pkt.data, F87_PKT_SIZE);
        fflush(stdout);
    }

    /* Unreachable, but good practice */
    f87_close(dev);
    f87_free_device_list(list);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Main — argument dispatch
 * ---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

    /* Handle --help / -h / --version early */
    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        usage(argv[0]);
        return 0;
    }
    if (strcmp(cmd, "--version") == 0) {
        printf("f87ctl %s\n", f87_version_string());
        return 0;
    }

    f87_ctx *ctx = f87_init();
    if (!ctx) {
        fprintf(stderr, "Failed to initialise libf87.\n");
        return 1;
    }

    int ret;

    if (strcmp(cmd, "list") == 0) {
        ret = cmd_list(ctx);

    } else if (strcmp(cmd, "info") == 0) {
        ret = cmd_info(ctx);

    } else if (strcmp(cmd, "brightness") == 0) {
        ret = cmd_brightness(ctx, argc - 2, argv + 2);

    } else if (strcmp(cmd, "off") == 0) {
        ret = cmd_off(ctx);

    } else if (strcmp(cmd, "effect") == 0) {
        ret = cmd_effect(ctx, argc - 2, argv + 2);

    } else if (strcmp(cmd, "key") == 0) {
        ret = cmd_key(ctx, argc - 2, argv + 2);

    } else if (strcmp(cmd, "raw") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: f87ctl raw send \"XX XX ...\"\n"
                            "       f87ctl raw listen\n");
            ret = 1;
        } else if (strcmp(argv[2], "send") == 0) {
            ret = cmd_raw_send(ctx, argc - 3, argv + 3);
        } else if (strcmp(argv[2], "listen") == 0) {
            ret = cmd_raw_listen(ctx);
        } else {
            fprintf(stderr, "Unknown raw subcommand '%s'.\n", argv[2]);
            ret = 1;
        }

    } else {
        fprintf(stderr, "Unknown command '%s'.\n\n", cmd);
        usage(argv[0]);
        ret = 1;
    }

    f87_exit(ctx);
    return ret;
}
