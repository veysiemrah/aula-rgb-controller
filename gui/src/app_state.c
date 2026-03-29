#include "app_state.h"
#include <stdio.h>
#include <string.h>

int f87_app_state_init(f87_app_state_t *state)
{
    memset(state, 0, sizeof(*state));
    state->status = F87_GUI_IDLE;
    snprintf(state->status_text, sizeof(state->status_text), "Baslatiliyor...");

    state->client = f87_client_connect();
    if (!state->client) {
        snprintf(state->status_text, sizeof(state->status_text),
                 "Daemon'a baglanilamadi");
        state->status = F87_GUI_ERROR;
        return -1;
    }

    f87_client_status_t st;
    if (f87_client_get_status(state->client, &st) < 0) {
        snprintf(state->status_text, sizeof(state->status_text),
                 "Daemon durumu alinamadi");
        state->status = F87_GUI_ERROR;
        return -1;
    }

    state->device_connected = st.connected;
    if (st.connected) {
        snprintf(state->status_text, sizeof(state->status_text),
                 "Bagli (daemon)");
        state->status = F87_GUI_IDLE;
    } else {
        snprintf(state->status_text, sizeof(state->status_text),
                 "Klavye bulunamadi");
        state->status = F87_GUI_ERROR;
    }

    return 0;
}

void f87_app_state_destroy(f87_app_state_t *state)
{
    if (state->client) {
        f87_client_disconnect(state->client);
        state->client = NULL;
    }
}

int f87_app_state_rescan(f87_app_state_t *state)
{
    if (!state->client) return -1;

    int rc = f87_client_rescan(state->client);
    if (rc < 0) {
        snprintf(state->status_text, sizeof(state->status_text),
                 "Tarama basarisiz");
        state->status = F87_GUI_ERROR;
        return -1;
    }

    state->device_connected = f87_client_is_connected(state->client) > 0;
    if (state->device_connected) {
        snprintf(state->status_text, sizeof(state->status_text),
                 "Bagli (daemon)");
        state->status = F87_GUI_IDLE;
    } else {
        snprintf(state->status_text, sizeof(state->status_text),
                 "Klavye bulunamadi");
        state->status = F87_GUI_ERROR;
    }
    return 0;
}

int f87_app_state_start_hw(f87_app_state_t *state, int mode_id,
                            uint8_t brightness, uint8_t speed,
                            uint8_t colorful, uint8_t r, uint8_t g, uint8_t b)
{
    if (!state->client) return -1;

    int rc = f87_client_set_effect(state->client, mode_id, brightness, speed,
                                    colorful, r, g, b);
    if (rc < 0) {
        snprintf(state->status_text, sizeof(state->status_text),
                 "Efekt gonderilemedi");
        state->status = F87_GUI_ERROR;
        return rc;
    }

    snprintf(state->status_text, sizeof(state->status_text),
             "%s calisiyor", f87_mode_name((f87_mode)mode_id));
    state->status = F87_GUI_RUNNING;
    state->current_effect_id = mode_id;
    strncpy(state->current_category, "hw", sizeof(state->current_category));
    return 0;
}

int f87_app_state_start_sw(f87_app_state_t *state, int effect_id,
                            const f87_anim_config_t *config)
{
    if (!state->client) return -1;

    int rc;
    if (effect_id == F87_SW_SENSOR) {
        rc = f87_client_set_sensor_effect(state->client,
                                           config->sensor_profile,
                                           config->sensor_config_path);
    } else if (effect_id >= 200) {
        rc = f87_client_set_music_effect(state->client, effect_id,
                                          config->brightness,
                                          config->color[0], config->color[1],
                                          config->color[2], config->gain);
    } else {
        rc = f87_client_set_sw_effect(state->client, effect_id,
                                       config->brightness, config->speed,
                                       config->color[0], config->color[1],
                                       config->color[2], config->fps);
    }

    if (rc < 0) {
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
    if (!state->client) return -1;

    int rc = f87_client_stop(state->client);
    if (rc < 0) {
        snprintf(state->status_text, sizeof(state->status_text),
                 "Durdurma hatasi");
        state->status = F87_GUI_ERROR;
        return rc;
    }

    snprintf(state->status_text, sizeof(state->status_text), "Bekleniyor");
    state->status = F87_GUI_IDLE;
    return 0;
}
