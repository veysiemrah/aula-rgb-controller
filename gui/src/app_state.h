#ifndef F87_APP_STATE_H
#define F87_APP_STATE_H

#include <f87/f87.h>
#include <f87/client.h>
#include <f87/logger.h>
#include <stdbool.h>

typedef enum {
    F87_GUI_IDLE,
    F87_GUI_RUNNING,
    F87_GUI_ERROR,
} f87_gui_status_t;

typedef struct {
    f87_client *client;
    f87_gui_status_t status;
    char status_text[256];
    int status_level;  /* F87_LOG_INFO, F87_LOG_WARN, F87_LOG_ERROR */
    int current_effect_id;
    char current_category[16];
    char current_sensor_profile[64];
    bool device_connected;
} f87_app_state_t;

int  f87_app_state_init(f87_app_state_t *state);
void f87_app_state_destroy(f87_app_state_t *state);
int  f87_app_state_rescan(f87_app_state_t *state);

int f87_app_state_start_hw(f87_app_state_t *state, int mode_id,
                            uint8_t brightness, uint8_t speed,
                            uint8_t colorful, uint8_t r, uint8_t g, uint8_t b);

int f87_app_state_start_sw(f87_app_state_t *state, int effect_id,
                            const f87_anim_config_t *config);

int f87_app_state_stop(f87_app_state_t *state);

/* Send per-key custom colors (88x3 RGB array) */
int f87_app_state_apply_custom(f87_app_state_t *state,
                                const uint8_t colors[][3], int count);

#endif /* F87_APP_STATE_H */
