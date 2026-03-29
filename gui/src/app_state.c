#include "app_state.h"
#include "protocol.h"
#include <stdio.h>
#include <string.h>

int f87_app_state_init(f87_app_state_t *state)
{
    memset(state, 0, sizeof(*state));
    state->status = F87_GUI_IDLE;
    snprintf(state->status_text, sizeof(state->status_text), "Baslatiliyor...");

    state->ctx = f87_init();
    if (!state->ctx) {
        snprintf(state->status_text, sizeof(state->status_text),
                 "libf87 baslatilamadi");
        state->status = F87_GUI_ERROR;
        return -1;
    }

    return f87_app_state_rescan(state);
}

void f87_app_state_destroy(f87_app_state_t *state)
{
    if (state->anim) {
        f87_anim_stop(state->anim);
        state->anim = NULL;
    }
    if (state->dev) {
        f87_close(state->dev);
        state->dev = NULL;
    }
    if (state->dev_list) {
        f87_free_device_list(state->dev_list);
        state->dev_list = NULL;
    }
    if (state->ctx) {
        f87_exit(state->ctx);
        state->ctx = NULL;
    }
}

int f87_app_state_rescan(f87_app_state_t *state)
{
    /* Close existing */
    if (state->dev) {
        f87_close(state->dev);
        state->dev = NULL;
    }
    if (state->dev_list) {
        f87_free_device_list(state->dev_list);
        state->dev_list = NULL;
    }

    int rc = f87_find_devices(state->ctx, &state->dev_list, &state->dev_count);
    if (rc < 0 || state->dev_count == 0) {
        snprintf(state->status_text, sizeof(state->status_text),
                 "Klavye bulunamadi");
        state->status = F87_GUI_ERROR;
        return -1;
    }

    state->dev = f87_open(state->ctx, &state->dev_list[0]);
    if (!state->dev) {
        snprintf(state->status_text, sizeof(state->status_text),
                 "Klavye acilamadi");
        state->status = F87_GUI_ERROR;
        return -1;
    }

    snprintf(state->status_text, sizeof(state->status_text),
             "Bagli: %s", state->dev_list[0].product);
    state->status = F87_GUI_IDLE;
    return 0;
}

int f87_app_state_start_hw(f87_app_state_t *state, int mode_id,
                            uint8_t brightness, uint8_t speed,
                            uint8_t colorful, uint8_t r, uint8_t g, uint8_t b)
{
    if (!state->dev) {
        snprintf(state->status_text, sizeof(state->status_text),
                 "Klavye bagli degil");
        state->status = F87_GUI_ERROR;
        return -1;
    }

    /* Stop any running animation first */
    if (state->anim) {
        f87_anim_stop(state->anim);
        state->anim = NULL;
    }

    f87_effect effect = {0};
    effect.mode = (f87_mode)mode_id;
    effect.brightness = brightness;
    effect.speed = speed;
    effect.colorful = colorful;
    effect.color1 = (f87_color){r, g, b};

    int rc = f87_set_effect(state->dev, &effect);
    if (rc < 0) {
        snprintf(state->status_text, sizeof(state->status_text),
                 "Efekt gonderilemedi: %s", f87_strerror(rc));
        state->status = F87_GUI_ERROR;
        return rc;
    }

    snprintf(state->status_text, sizeof(state->status_text),
             "%s calisiyor", f87_mode_name(effect.mode));
    state->status = F87_GUI_RUNNING;
    state->current_effect_id = mode_id;
    strncpy(state->current_category, "hw", sizeof(state->current_category));
    return 0;
}

int f87_app_state_start_sw(f87_app_state_t *state, int effect_id,
                            const f87_anim_config_t *config)
{
    if (!state->dev) {
        snprintf(state->status_text, sizeof(state->status_text),
                 "Klavye bagli degil");
        state->status = F87_GUI_ERROR;
        return -1;
    }

    /* Stop any running animation first */
    if (state->anim) {
        f87_anim_stop(state->anim);
        state->anim = NULL;
    }

    state->anim = f87_anim_start(state->dev, (f87_sw_effect_id)effect_id, config);
    if (!state->anim) {
        snprintf(state->status_text, sizeof(state->status_text),
                 "Animasyon baslatilamadi");
        state->status = F87_GUI_ERROR;
        return -1;
    }

    snprintf(state->status_text, sizeof(state->status_text),
             "%s calisiyor", f87_sw_effect_name((f87_sw_effect_id)effect_id));
    state->status = F87_GUI_RUNNING;
    state->current_effect_id = effect_id;
    return 0;
}

int f87_app_state_stop(f87_app_state_t *state)
{
    if (state->anim) {
        int rc = f87_anim_stop(state->anim);
        state->anim = NULL;
        if (rc < 0) {
            snprintf(state->status_text, sizeof(state->status_text),
                     "Durdurma hatasi: %s", f87_strerror(rc));
            state->status = F87_GUI_ERROR;
            return rc;
        }
    }

    snprintf(state->status_text, sizeof(state->status_text), "Bekleniyor");
    state->status = F87_GUI_IDLE;
    return 0;
}
