#include "animate_internal.h"
#include "f87/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <poll.h>
#include <linux/input.h>
#include <sys/ioctl.h>

#define F87_ANIM_MAX_FPS_SW   25
#define F87_ANIM_MAX_FPS_MU   30
#define F87_ANIM_FRAME_US_25  40000  /* ~25fps = 40ms — safe USB limit for SW */
#define F87_ANIM_FRAME_US_30  33333  /* ~30fps = 33ms — music effects */

uint64_t f87_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

uint32_t f87_effect_rand(uint32_t *seed)
{
    *seed = *seed * 1103515245u + 12345u;
    return (*seed >> 16) & 0x7FFF;
}

/* True if the event device reports key presses (has KEY_A). */
static int has_letter_keys(int fd)
{
    unsigned long evbits = 0;
    if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), &evbits) < 0 ||
        !(evbits & (1UL << EV_KEY)))
        return 0;

    unsigned long keybits[(KEY_MAX + 8 * sizeof(unsigned long) - 1) /
                          (8 * sizeof(unsigned long))] = {0};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) < 0)
        return 0;

    return (keybits[KEY_A / (8 * sizeof(unsigned long))] >>
            (KEY_A % (8 * sizeof(unsigned long)))) & 1UL;
}

/* True if the event device belongs to the AULA keyboard. */
static int is_aula_keyboard(int fd)
{
    struct input_id id;
    if (ioctl(fd, EVIOCGID, &id) == 0 &&
        ((id.vendor == 0x258A && id.product == 0x010C) ||
         (id.vendor == 0x3554 && id.product == 0xFA09)))
        return 1;

    char name[256] = "";
    ioctl(fd, EVIOCGNAME(sizeof(name)), name);
    return strstr(name, "BY Tech Gaming Keyboard") != NULL;
}

/*
 * Open the keyboard input devices for reactive effects.
 *
 * The AULA keyboard reports key presses on more than one event device:
 * the boot keyboard on USB interface 0 and an NKRO keyboard collection on
 * interface 1 (both named "BY Tech Gaming Keyboard"). Which one carries
 * the presses depends on the keyboard mode, so listen on all of them.
 * Falls back to the first other keyboard if no AULA device is found.
 */
static void open_keyboard_inputs(f87_anim_ctx_t *ctx)
{
    char path[64];
    int fallback_fd = -1;

    ctx->num_inputs = 0;
    for (int i = 0; i < 64 && ctx->num_inputs < F87_MAX_INPUTS; i++) {
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;

        if (!has_letter_keys(fd)) {
            close(fd);
        } else if (is_aula_keyboard(fd)) {
            ctx->input_fds[ctx->num_inputs++] = fd;
        } else if (fallback_fd < 0) {
            fallback_fd = fd;
        } else {
            close(fd);
        }
    }

    if (ctx->num_inputs == 0 && fallback_fd >= 0)
        ctx->input_fds[ctx->num_inputs++] = fallback_fd;
    else if (fallback_fd >= 0)
        close(fallback_fd);

    F87_INFO(F87_SRC_EFFECT, "Reactive input: %d keyboard device(s)",
             ctx->num_inputs);
}

static void close_keyboard_inputs(f87_anim_ctx_t *ctx)
{
    for (int i = 0; i < ctx->num_inputs; i++)
        close(ctx->input_fds[i]);
    ctx->num_inputs = 0;
}

/* Map Linux keycode to F87 key_id (-1 if not found).
 * key_id values match f87_key_layout[] indices in protocol.c. */
static int keycode_to_key_id(int keycode)
{
    static const struct { int keycode; int key_id; } map[] = {
        /* Row 0: Function row */
        {KEY_ESC, 0}, {KEY_F1, 1}, {KEY_F2, 2}, {KEY_F3, 3}, {KEY_F4, 4},
        {KEY_F5, 5}, {KEY_F6, 6}, {KEY_F7, 7}, {KEY_F8, 8}, {KEY_F9, 9},
        {KEY_F10, 10}, {KEY_F11, 11}, {KEY_F12, 12},
        /* Row 1: Number row */
        {KEY_GRAVE, 13}, {KEY_1, 14}, {KEY_2, 15}, {KEY_3, 16}, {KEY_4, 17},
        {KEY_5, 18}, {KEY_6, 19}, {KEY_7, 20}, {KEY_8, 21}, {KEY_9, 22},
        {KEY_0, 23}, {KEY_MINUS, 24}, {KEY_EQUAL, 25}, {KEY_BACKSPACE, 26},
        {KEY_SYSRQ, 27}, {KEY_SCROLLLOCK, 28}, {KEY_PAUSE, 29},
        /* Row 2: QWERTY row */
        {KEY_TAB, 30}, {KEY_Q, 31}, {KEY_W, 32}, {KEY_E, 33}, {KEY_R, 34},
        {KEY_T, 35}, {KEY_Y, 36}, {KEY_U, 37}, {KEY_I, 38}, {KEY_O, 39},
        {KEY_P, 40}, {KEY_LEFTBRACE, 41}, {KEY_RIGHTBRACE, 42}, {KEY_ENTER, 43},
        {KEY_DELETE, 44}, {KEY_INSERT, 45}, {KEY_HOME, 46}, {KEY_PAGEUP, 47},
        /* Row 3: Home row */
        {KEY_CAPSLOCK, 48}, {KEY_A, 49}, {KEY_S, 50}, {KEY_D, 51}, {KEY_F, 52},
        {KEY_G, 53}, {KEY_H, 54}, {KEY_J, 55}, {KEY_K, 56}, {KEY_L, 57},
        {KEY_SEMICOLON, 58}, {KEY_APOSTROPHE, 59}, {KEY_BACKSLASH, 60},
        {KEY_END, 61}, {KEY_PAGEDOWN, 62},
        /* Row 4: Shift row */
        {KEY_LEFTSHIFT, 63}, {KEY_Z, 64}, {KEY_X, 65}, {KEY_C, 66},
        {KEY_V, 67}, {KEY_B, 68}, {KEY_N, 69}, {KEY_M, 70},
        {KEY_COMMA, 71}, {KEY_DOT, 72}, {KEY_SLASH, 73}, {KEY_RIGHTSHIFT, 74},
        {KEY_UP, 75},
        /* Row 5: Bottom row */
        {KEY_LEFTCTRL, 77}, {KEY_LEFTMETA, 78}, {KEY_LEFTALT, 79},
        {KEY_SPACE, 80}, {KEY_RIGHTALT, 81},
        {KEY_COMPOSE, 83}, {KEY_RIGHTCTRL, 76},
        {KEY_LEFT, 84}, {KEY_DOWN, 85}, {KEY_RIGHT, 86},
        {-1, -1}
    };

    for (int i = 0; map[i].keycode >= 0; i++) {
        if (map[i].keycode == keycode)
            return map[i].key_id;
    }
    return -1;
}

static void *anim_thread_func(void *arg)
{
    f87_anim_ctx_t *ctx = arg;

    f87_direct_mode_enable(ctx->dev);
    /* Direct mode enable may report errors on F87 TK but still work.
       We proceed regardless and check if frame sending works. */

    int rc;
    f87_frame_t frame;
    f87_audio_data_t audio;

    while (atomic_load(&ctx->running)) {
        uint64_t t0 = f87_time_us();

        /* Read latest audio data if available */
        const f87_audio_data_t *audio_ptr = NULL;
        if (ctx->audio_ring && f87_ring_read_latest(ctx->audio_ring, &audio))
            audio_ptr = &audio;

        /* Poll input for reactive effects */
        for (int n = 0; n < ctx->num_inputs; n++) {
            struct pollfd pfd = { .fd = ctx->input_fds[n], .events = POLLIN };
            while (poll(&pfd, 1, 0) > 0) {
                struct input_event ev;
                if (read(pfd.fd, &ev, sizeof(ev)) != (ssize_t)sizeof(ev))
                    break;
                if (ev.type == EV_KEY && ev.value == 1) {
                    pthread_mutex_lock(&ctx->effect_mutex);
                    if (ctx->active_effect && ctx->active_effect->on_key) {
                        int key_id = keycode_to_key_id(ev.code);
                        if (key_id >= 0)
                            ctx->active_effect->on_key(&ctx->effect_ctx, key_id);
                    }
                    pthread_mutex_unlock(&ctx->effect_mutex);
                }
            }
        }

        /* Render current effect */
        pthread_mutex_lock(&ctx->effect_mutex);
        if (ctx->active_effect && ctx->active_effect->render) {
            memset(&frame, 0, sizeof(frame));
            ctx->effect_ctx.frame_count++;
            ctx->active_effect->render(&ctx->effect_ctx, &frame, audio_ptr);
        }
        pthread_mutex_unlock(&ctx->effect_mutex);

        /* Convert frame to direct mode and send */
        f87_color colors[F87_KEY_COUNT];
        for (int i = 0; i < F87_KEY_COUNT; i++) {
            colors[i].r = frame.keys[i][0];
            colors[i].g = frame.keys[i][1];
            colors[i].b = frame.keys[i][2];
        }

        rc = f87_direct_send_frame(ctx->dev, colors, f87_led_index, F87_KEY_COUNT);

        /* Mandatory post-transfer delay — prevents USB overload */
        usleep(F87_CMD_DELAY_US);

        if (rc < 0) {
            /* USB transient error — let keyboard settle, then re-enable direct mode */
            usleep(100000);  /* 100ms settle */
            f87_direct_mode_enable(ctx->dev);
            usleep(50000);   /* 50ms after mode enable */

            /* Retry with exponential backoff: 20ms, 40ms, 80ms, 160ms, 320ms, 640ms */
            int retries = 6;
            useconds_t backoff = 20000;
            while (rc < 0 && retries-- > 0) {
                usleep(backoff);
                rc = f87_direct_send_frame(ctx->dev, colors, f87_led_index, F87_KEY_COUNT);
                usleep(F87_CMD_DELAY_US);
                backoff *= 2;
            }
            if (rc < 0) {
                F87_WARN(F87_SRC_EFFECT, "animation USB error after retries, stopping");
                atomic_store(&ctx->error, F87_ERR_IO);
                atomic_store(&ctx->running, false);
                break;
            }
            F87_DEBUG(F87_SRC_EFFECT, "animation recovered after USB transient error");
        }

        /* Sleep to maintain target frame rate */
        uint64_t target_us = ctx->frame_time_us;
        uint64_t elapsed = f87_time_us() - t0;
        if (elapsed < target_us)
            usleep((useconds_t)(target_us - elapsed));

        /* FPS counter (logs every 3 seconds) */
        if (ctx->effect_ctx.frame_count % 90 == 0 && ctx->effect_ctx.frame_count > 0) {
            uint64_t total = f87_time_us() - ctx->effect_ctx.start_time_us;
            float fps = (float)ctx->effect_ctx.frame_count / ((float)total / 1000000.0f);
            F87_DEBUG(F87_SRC_EFFECT, "%.1f fps (%lu frames)", fps, (unsigned long)ctx->effect_ctx.frame_count);
        }
    }

    /* Always try to disable direct mode — even after error.
     * On error the keyboard may have reset, so failure here is OK. */
    f87_direct_mode_disable(ctx->dev);
    return NULL;
}

f87_anim_ctx_t *f87_anim_start(f87_device *dev, f87_sw_effect_id effect_id,
                                const f87_anim_config_t *config)
{
    if (!dev) { F87_ERROR(F87_SRC_EFFECT, "anim_start: dev is NULL"); return NULL; }

    const f87_sw_effect_t *effect = f87_sw_find_effect(effect_id);
    if (!effect) { F87_ERROR(F87_SRC_EFFECT, "anim_start: effect %d not found", effect_id); return NULL; }

    f87_anim_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->dev = dev;
    ctx->num_inputs = 0;
    pthread_mutex_init(&ctx->effect_mutex, NULL);

    /* Apply config with defaults */
    if (config) {
        ctx->config = *config;
    } else {
        ctx->config.brightness = 3;
        ctx->config.speed = 2;
        ctx->config.audio_source = F87_AUDIO_MONITOR;
    }

    /* Initialize effect context */
    ctx->effect_ctx.dev = dev;
    if (config)
        memcpy(ctx->effect_ctx.base_color, config->color, 3);
    else {
        ctx->effect_ctx.base_color[0] = 255;
        ctx->effect_ctx.base_color[1] = 80;
        ctx->effect_ctx.base_color[2] = 0;
    }
    ctx->effect_ctx.brightness = ctx->config.brightness;
    ctx->effect_ctx.speed = ctx->config.speed;
    ctx->effect_ctx.gain = ctx->config.gain;
    ctx->effect_ctx.sensor_profile = ctx->config.sensor_profile;
    ctx->effect_ctx.sensor_config_path = ctx->config.sensor_config_path;
    ctx->effect_ctx.frame_count = 0;
    ctx->effect_ctx.start_time_us = f87_time_us();
    ctx->effect_ctx.effect_data = NULL;

    /* Init effect */
    ctx->active_effect = effect;
    if (effect->init) {
        int rc = effect->init(&ctx->effect_ctx);
        if (rc < 0) {
            F87_ERROR(F87_SRC_EFFECT, "anim_start: effect init failed (%d)", rc);
            pthread_mutex_destroy(&ctx->effect_mutex);
            free(ctx);
            return NULL;
        }
    }

    /* Open input device if needed */
    if (effect->needs_input)
        open_keyboard_inputs(ctx);  /* Non-fatal if input not available */

    /* Set frame rate: override from config, or auto (25fps SW, 30fps music) */
    if (ctx->config.fps > 0) {
        int max_fps = effect->needs_audio ? F87_ANIM_MAX_FPS_MU : F87_ANIM_MAX_FPS_SW;
        int fps = ctx->config.fps > max_fps ? max_fps : ctx->config.fps;
        ctx->frame_time_us = 1000000ULL / (uint64_t)fps;
    } else {
        ctx->frame_time_us = effect->needs_audio ? F87_ANIM_FRAME_US_30 : F87_ANIM_FRAME_US_25;
    }

    atomic_store(&ctx->running, true);

    /* Start audio thread if needed */
#ifdef F87_HAS_AUDIO
    if (effect->needs_audio) {
        ctx->audio_ring = calloc(1, sizeof(f87_audio_ring_t));
        if (!ctx->audio_ring) {
            if (effect->destroy) effect->destroy(&ctx->effect_ctx);
            close_keyboard_inputs(ctx);
            pthread_mutex_destroy(&ctx->effect_mutex);
            free(ctx);
            return NULL;
        }
        f87_ring_init(ctx->audio_ring);

        extern int f87_audio_thread_start(f87_anim_ctx_t *ctx);
        int rc = f87_audio_thread_start(ctx);
        if (rc < 0) {
            F87_ERROR(F87_SRC_AUDIO, "anim_start: audio thread failed");
            free(ctx->audio_ring);
            if (effect->destroy) effect->destroy(&ctx->effect_ctx);
            close_keyboard_inputs(ctx);
            pthread_mutex_destroy(&ctx->effect_mutex);
            free(ctx);
            return NULL;
        }
    }
#else
    if (effect->needs_audio) {
        if (effect->destroy) effect->destroy(&ctx->effect_ctx);
        close_keyboard_inputs(ctx);
        pthread_mutex_destroy(&ctx->effect_mutex);
        free(ctx);
        return NULL;
    }
#endif

    /* Start animation thread */
    int rc = pthread_create(&ctx->anim_thread, NULL, anim_thread_func, ctx);
    if (rc != 0) {
        atomic_store(&ctx->running, false);
        if (effect->destroy) effect->destroy(&ctx->effect_ctx);
#ifdef F87_HAS_AUDIO
        if (ctx->audio_ring) {
            pthread_join(ctx->audio_thread, NULL);
            free(ctx->audio_ring);
        }
#endif
        close_keyboard_inputs(ctx);
        pthread_mutex_destroy(&ctx->effect_mutex);
        free(ctx);
        return NULL;
    }

    return ctx;
}

int f87_anim_stop(f87_anim_ctx_t *ctx)
{
    if (!ctx) return F87_ERR_INIT;

    atomic_store(&ctx->running, false);

    pthread_join(ctx->anim_thread, NULL);

#ifdef F87_HAS_AUDIO
    if (ctx->audio_ring) {
        pthread_join(ctx->audio_thread, NULL);
        free(ctx->audio_ring);
    }
#endif

    pthread_mutex_lock(&ctx->effect_mutex);
    if (ctx->active_effect && ctx->active_effect->destroy)
        ctx->active_effect->destroy(&ctx->effect_ctx);
    pthread_mutex_unlock(&ctx->effect_mutex);

    close_keyboard_inputs(ctx);

    pthread_mutex_destroy(&ctx->effect_mutex);

    int err = atomic_load(&ctx->error);
    free(ctx);
    return err;
}

int f87_anim_set_effect(f87_anim_ctx_t *ctx, f87_sw_effect_id effect_id)
{
    if (!ctx) return F87_ERR_INIT;

    const f87_sw_effect_t *new_effect = f87_sw_find_effect(effect_id);
    if (!new_effect) return F87_ERR_INIT;

    pthread_mutex_lock(&ctx->effect_mutex);

    if (ctx->active_effect && ctx->active_effect->destroy)
        ctx->active_effect->destroy(&ctx->effect_ctx);

    ctx->effect_ctx.frame_count = 0;
    ctx->effect_ctx.start_time_us = f87_time_us();
    ctx->effect_ctx.effect_data = NULL;

    /* Open input device if new effect needs it and not yet open */
    if (new_effect->needs_input && ctx->num_inputs == 0)
        open_keyboard_inputs(ctx);

    /* Close input devices if new effect doesn't need them */
    if (!new_effect->needs_input)
        close_keyboard_inputs(ctx);

    ctx->active_effect = new_effect;
    if (new_effect->init) {
        int rc = new_effect->init(&ctx->effect_ctx);
        if (rc < 0) {
            ctx->active_effect = NULL;
            pthread_mutex_unlock(&ctx->effect_mutex);
            return rc;
        }
    }

    pthread_mutex_unlock(&ctx->effect_mutex);
    return F87_OK;
}

int f87_anim_set_color(f87_anim_ctx_t *ctx, uint8_t r, uint8_t g, uint8_t b)
{
    if (!ctx) return F87_ERR_INIT;
    ctx->effect_ctx.base_color[0] = r;
    ctx->effect_ctx.base_color[1] = g;
    ctx->effect_ctx.base_color[2] = b;
    return F87_OK;
}

int f87_anim_is_running(f87_anim_ctx_t *ctx)
{
    return ctx && atomic_load(&ctx->running);
}

int f87_anim_get_error(f87_anim_ctx_t *ctx)
{
    return ctx ? atomic_load(&ctx->error) : F87_ERR_INIT;
}
