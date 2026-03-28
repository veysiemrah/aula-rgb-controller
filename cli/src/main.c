/*
 * f87ctl -- CLI for controlling Aula F87 Pro keyboards
 *
 * Uses the public libf87 API for device management, lighting, and effects.
 * Includes the internal protocol header only for raw HID packet commands.
 */

#include <f87/f87.h>

/* Internal header -- needed only for raw send/listen (f87_pkt_send/recv) */
#include "protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>

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
        "  brightness <1-4>                  Set brightness (1=min, 4=max)\n"
        "  off                               Turn off lighting\n"
        "  color <RRGGBB>                    Set all keys to colour (static)\n"
        "  effect <name> [RRGGBB]            Set hardware effect\n"
        "  key set <KEY> <RRGGBB>            Set single key colour\n"
        "  key set-all <RRGGBB>              Set all keys to colour\n"
        "  raw send \"XX XX XX ...\"            Send raw HID feature report\n"
        "  raw listen                        Read HID feature report\n"
        "\n"
        "  animate <effect> [RRGGBB] [opts]   Software animation (foreground)\n"
        "  music <mode> [opts]               Music-reactive mode (foreground)\n"
        "\n"
        "HW Effects: off, static, breathing, wave, spectrum, rain, ripple,\n"
        "            starlight, snake, aurora, reactive, marquee, circle,\n"
        "            raindown, center, custom\n"
        "\n"
        "SW Effects: fire, matrix, plasma, heatmap, radar, lightning,\n"
        "            explode, ripple-sw, typewriter, life\n"
        "\n"
        "Music modes: spectrum, beat, energy, vu, freqmap\n"
        "\n", progname);
}

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
        printf("  [%d] %s -- %s\n"
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
    printf("  Protocol:     HID Feature Reports (%d bytes)\n", F87_REPORT_SIZE);

    /* Read current effect and brightness */
    f87_effect cur_effect;
    if (f87_get_current_effect(dev, &cur_effect) == 0) {
        printf("  Effect:       %s (id=%d)\n",
               f87_mode_name(cur_effect.mode), cur_effect.mode);
        printf("  Brightness:   %d/4\n", cur_effect.brightness);
        printf("  Speed:        %d/4\n", cur_effect.speed);
        printf("  Color mode:   %s\n", cur_effect.colorful ? "colorful" : "single");
    }

    f87_close(dev);
    f87_free_device_list(list);
    return 0;
}

static int cmd_brightness(f87_ctx *ctx, int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "Usage: f87ctl brightness <1-4>\n");
        return 1;
    }

    int level = atoi(argv[0]);
    if (level < 1 || level > 4) {
        fprintf(stderr, "Brightness must be 1-4, got %d.\n", level);
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

    printf("Brightness set to %d/4.\n", level);
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

static int cmd_color(f87_ctx *ctx, int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "Usage: f87ctl color <RRGGBB>\n");
        return 1;
    }

    f87_color color;
    if (parse_color(argv[0], &color) < 0) {
        fprintf(stderr, "Invalid colour '%s'. Expected RRGGBB.\n", argv[0]);
        return 1;
    }

    f87_device_info *list = NULL;
    int count = 0;
    f87_device *dev = open_first_device(ctx, &list, &count);
    if (!dev)
        return 1;

    f87_set_all_keys(dev, color);
    int rc = f87_apply(dev);
    if (rc < 0) {
        fprintf(stderr, "Error applying colour: %s\n", f87_strerror(rc));
        f87_close(dev);
        f87_free_device_list(list);
        return 1;
    }

    printf("All keys set to #%02X%02X%02X.\n", color.r, color.g, color.b);
    f87_close(dev);
    f87_free_device_list(list);
    return 0;
}

static int cmd_effect(f87_ctx *ctx, int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "Usage: f87ctl effect <name> [RRGGBB]\n");
        return 1;
    }

    const char *name = argv[0];
    f87_mode mode = F87_MODE_OFF;

    struct { const char *name; f87_mode mode; } modes[] = {
        {"off",       F87_MODE_OFF},
        {"static",    F87_MODE_STATIC},
        {"breathing", F87_MODE_BREATHING},
        {"wave",      F87_MODE_WAVE},
        {"spectrum",  F87_MODE_SPECTRUM},
        {"rain",      F87_MODE_RAIN},
        {"ripple",    F87_MODE_RIPPLE},
        {"starlight", F87_MODE_STARLIGHT},
        {"snake",     F87_MODE_SNAKE},
        {"aurora",    F87_MODE_AURORA},
        {"reactive",  F87_MODE_REACTIVE},
        {"marquee",   F87_MODE_MARQUEE},
        {"circle",    F87_MODE_CIRCLE},
        {"raindown",  F87_MODE_RAINDOWN},
        {"center",    F87_MODE_RIPPLE_CENTER},
        {"custom",    F87_MODE_CUSTOM},
        {NULL, 0}
    };

    int found = 0;
    for (int i = 0; modes[i].name; i++) {
        if (strcasecmp(name, modes[i].name) == 0) {
            mode = modes[i].mode;
            found = 1;
            break;
        }
    }
    if (!found) {
        fprintf(stderr, "Unknown effect '%s'.\n", name);
        return 1;
    }

    f87_color color = F87_COLOR_RED;
    uint8_t bright = F87_BRIGHTNESS_MAX;
    uint8_t spd = 0xFF; /* don't change */
    uint8_t colorful = 0; /* default: single color */

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--brightness") == 0 && i + 1 < argc) {
            bright = (uint8_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--speed") == 0 && i + 1 < argc) {
            spd = (uint8_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--colorful") == 0) {
            colorful = 1;
        } else if (argv[i][0] != '-') {
            parse_color(argv[i], &color);
        }
    }

    f87_device_info *list = NULL;
    int count = 0;
    f87_device *dev = open_first_device(ctx, &list, &count);
    if (!dev)
        return 1;

    f87_effect effect = {0};
    effect.mode = mode;
    effect.brightness = bright;
    effect.speed = spd;
    effect.colorful = colorful;
    effect.color1 = color;

    int rc = f87_set_effect(dev, &effect);
    if (rc < 0) {
        fprintf(stderr, "Error setting effect: %s\n", f87_strerror(rc));
        f87_close(dev);
        f87_free_device_list(list);
        return 1;
    }

    printf("Effect set to %s", f87_mode_name(mode));
    if (mode != F87_MODE_OFF)
        printf(" (#%02X%02X%02X)", color.r, color.g, color.b);
    printf(".\n");

    f87_close(dev);
    f87_free_device_list(list);
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

        f87_set_all_keys(dev, color);
        int rc = f87_apply(dev);
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
                if (layout[i].name)
                    fprintf(stderr, "  %s\n", layout[i].name);
            }
            f87_close(dev);
            f87_free_device_list(list);
            return 1;
        }

        f87_set_key_color(dev, (uint8_t)key_id, color);
        int rc = f87_apply(dev);
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
        fprintf(stderr, "Unknown key subcommand '%s'.\n", subcmd);
        return 1;
    }
}

static void print_hex_dump(const uint8_t *data, int len)
{
    int last_nonzero = 0;
    for (int i = 0; i < len; i++) {
        if (data[i] != 0)
            last_nonzero = i;
    }
    int print_len = last_nonzero + 1;
    if (print_len < 16)
        print_len = 16;
    if (print_len > len)
        print_len = len;

    for (int i = 0; i < print_len; i++) {
        printf("%02X", data[i]);
        if (i < print_len - 1)
            printf(" ");
    }
    if (print_len < len)
        printf(" ... (%d more zero bytes)", len - print_len);
    printf("\n");
}

/* ---------------------------------------------------------------------------
 * Animation / Music commands
 * ---------------------------------------------------------------------------*/

static f87_anim_ctx_t *g_anim_ctx = NULL;
static f87_device *g_anim_dev = NULL;
static f87_device_info *g_anim_list = NULL;

static void anim_signal_handler(int sig)
{
    (void)sig;
    if (g_anim_ctx) {
        f87_anim_stop(g_anim_ctx);
        g_anim_ctx = NULL;
    }
    if (g_anim_dev) {
        f87_close(g_anim_dev);
        g_anim_dev = NULL;
    }
    if (g_anim_list) {
        f87_free_device_list(g_anim_list);
        g_anim_list = NULL;
    }
    _exit(0);
}

static int parse_sw_effect(const char *name)
{
    struct { const char *name; f87_sw_effect_id id; } map[] = {
        {"fire",       F87_SW_FIRE},
        {"matrix",     F87_SW_MATRIX},
        {"plasma",     F87_SW_PLASMA},
        {"heatmap",    F87_SW_HEATMAP},
        {"radar",      F87_SW_RADAR},
        {"lightning",  F87_SW_LIGHTNING},
        {"explode",    F87_SW_EXPLODE},
        {"ripple-sw",  F87_SW_RIPPLE},
        {"typewriter", F87_SW_TYPEWRITER},
        {"life",       F87_SW_LIFE},
        {NULL, 0}
    };
    for (int i = 0; map[i].name; i++) {
        if (strcmp(name, map[i].name) == 0)
            return (int)map[i].id;
    }
    return -1;
}

static int parse_music_mode(const char *name)
{
    struct { const char *name; f87_sw_effect_id id; } map[] = {
        {"spectrum",  F87_MU_SPECTRUM},
        {"beat",      F87_MU_BEAT},
        {"energy",    F87_MU_ENERGY},
        {"vu",        F87_MU_VU},
        {"freqmap",   F87_MU_FREQ_MAP},
        {NULL, 0}
    };
    for (int i = 0; map[i].name; i++) {
        if (strcmp(name, map[i].name) == 0)
            return (int)map[i].id;
    }
    return -1;
}

static int cmd_animate(f87_ctx *ctx, int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "Usage: f87ctl animate <effect> [RRGGBB] [--speed 0-4] [--brightness 1-4]\n");
        return 1;
    }

    int effect_id = parse_sw_effect(argv[0]);
    if (effect_id < 0) {
        fprintf(stderr, "Unknown effect: %s\n", argv[0]);
        return 1;
    }

    f87_anim_config_t config = {
        .color = {255, 80, 0},
        .brightness = 3,
        .speed = 2,
        .audio_source = F87_AUDIO_MONITOR,
    };

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--speed") == 0 && i + 1 < argc) {
            config.speed = (uint8_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--brightness") == 0 && i + 1 < argc) {
            config.brightness = (uint8_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            config.fps = atoi(argv[++i]);
        } else if (strlen(argv[i]) == 6 && argv[i][0] != '-') {
            f87_color c;
            if (parse_color(argv[i], &c) == 0) {
                config.color[0] = c.r;
                config.color[1] = c.g;
                config.color[2] = c.b;
            }
        }
    }

    int count = 0;
    f87_device *dev = open_first_device(ctx, &g_anim_list, &count);
    if (!dev)
        return 1;
    g_anim_dev = dev;

    signal(SIGINT, anim_signal_handler);
    signal(SIGTERM, anim_signal_handler);

    g_anim_ctx = f87_anim_start(dev, (f87_sw_effect_id)effect_id, &config);
    if (!g_anim_ctx) {
        fprintf(stderr, "Failed to start animation\n");
        f87_close(dev);
        f87_free_device_list(g_anim_list);
        g_anim_dev = NULL;
        g_anim_list = NULL;
        return 1;
    }

    printf("Running '%s'... (Ctrl+C to stop)\n", f87_sw_effect_name((f87_sw_effect_id)effect_id));

    while (f87_anim_is_running(g_anim_ctx))
        usleep(100000);

    int err = f87_anim_get_error(g_anim_ctx);
    f87_anim_stop(g_anim_ctx);
    g_anim_ctx = NULL;

    if (err < 0)
        fprintf(stderr, "Animation error: %s\n", f87_strerror(err));

    f87_close(dev);
    f87_free_device_list(g_anim_list);
    g_anim_dev = NULL;
    g_anim_list = NULL;
    return err < 0 ? 1 : 0;
}

static int cmd_music(f87_ctx *ctx, int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "Usage: f87ctl music <mode> [--source monitor|mic]\n");
        return 1;
    }

    int effect_id = parse_music_mode(argv[0]);
    if (effect_id < 0) {
        fprintf(stderr, "Unknown music mode: %s\n", argv[0]);
        return 1;
    }

    f87_anim_config_t config = {
        .color = {0, 128, 255},
        .brightness = 4,
        .speed = 2,
        .audio_source = F87_AUDIO_MONITOR,
    };

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--source") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "mic") == 0)
                config.audio_source = F87_AUDIO_MIC;
        } else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            config.fps = atoi(argv[++i]);
        } else if (strlen(argv[i]) == 6 && argv[i][0] != '-') {
            f87_color c;
            if (parse_color(argv[i], &c) == 0) {
                config.color[0] = c.r;
                config.color[1] = c.g;
                config.color[2] = c.b;
            }
        }
    }

    int count = 0;
    f87_device *dev = open_first_device(ctx, &g_anim_list, &count);
    if (!dev)
        return 1;
    g_anim_dev = dev;

    signal(SIGINT, anim_signal_handler);
    signal(SIGTERM, anim_signal_handler);

    g_anim_ctx = f87_anim_start(dev, (f87_sw_effect_id)effect_id, &config);
    if (!g_anim_ctx) {
        fprintf(stderr, "Failed to start music mode (audio support compiled in?)\n");
        f87_close(dev);
        f87_free_device_list(g_anim_list);
        g_anim_dev = NULL;
        g_anim_list = NULL;
        return 1;
    }

    printf("Running music mode '%s'... (Ctrl+C to stop)\n",
           f87_sw_effect_name((f87_sw_effect_id)effect_id));

    while (f87_anim_is_running(g_anim_ctx))
        usleep(100000);

    int err = f87_anim_get_error(g_anim_ctx);
    f87_anim_stop(g_anim_ctx);
    g_anim_ctx = NULL;

    if (err < 0)
        fprintf(stderr, "Music mode error: %s\n", f87_strerror(err));

    f87_close(dev);
    f87_free_device_list(g_anim_list);
    g_anim_dev = NULL;
    g_anim_list = NULL;
    return err < 0 ? 1 : 0;
}

static int cmd_raw_send(f87_ctx *ctx, int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "Usage: f87ctl raw send \"XX XX XX ...\"\n");
        return 1;
    }

    char buf[2048] = {0};
    for (int i = 0; i < argc; i++) {
        if (i > 0)
            strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
        strncat(buf, argv[i], sizeof(buf) - strlen(buf) - 1);
    }

    f87_packet pkt;
    f87_pkt_init(&pkt);

    int byte_count = 0;
    char *tok = strtok(buf, " ");
    while (tok && byte_count < F87_REPORT_SIZE) {
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

    printf("Sending %d byte(s) as %d-byte feature report:\n  ",
           byte_count, F87_REPORT_SIZE);
    print_hex_dump(pkt.data, F87_REPORT_SIZE);

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
    printf("Sent (%d bytes transferred).\n", rc);

    f87_packet resp;
    rc = f87_pkt_recv(dev, &resp, F87_TIMEOUT_MS);
    if (rc < 0) {
        printf("No response (timeout or error).\n");
    } else {
        printf("Response (%d bytes):\n  ", rc);
        print_hex_dump(resp.data, F87_REPORT_SIZE);
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

    printf("Reading HID feature report...\n\n");

    f87_packet pkt;
    int rc = f87_pkt_recv(dev, &pkt, 5000);
    if (rc < 0) {
        printf("No response (timeout or error).\n");
    } else {
        printf("Response (%d bytes):\n  ", rc);
        print_hex_dump(pkt.data, F87_REPORT_SIZE);
    }

    f87_close(dev);
    f87_free_device_list(list);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Main
 * ---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

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

    } else if (strcmp(cmd, "color") == 0 || strcmp(cmd, "colour") == 0) {
        ret = cmd_color(ctx, argc - 2, argv + 2);

    } else if (strcmp(cmd, "effect") == 0) {
        ret = cmd_effect(ctx, argc - 2, argv + 2);

    } else if (strcmp(cmd, "key") == 0) {
        ret = cmd_key(ctx, argc - 2, argv + 2);

    } else if (strcmp(cmd, "animate") == 0) {
        ret = cmd_animate(ctx, argc - 2, argv + 2);

    } else if (strcmp(cmd, "music") == 0) {
        ret = cmd_music(ctx, argc - 2, argv + 2);

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
