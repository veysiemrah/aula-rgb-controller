#ifndef F87D_EFFECT_MANAGER_H
#define F87D_EFFECT_MANAGER_H

#include <f87/f87.h>
#include <stdbool.h>

typedef enum {
    F87D_CAT_NONE = 0,
    F87D_CAT_HW,
    F87D_CAT_SW,
    F87D_CAT_MUSIC,
    F87D_CAT_SENSOR,
} f87d_effect_category_t;

typedef struct {
    f87_anim_ctx_t *anim;
    f87d_effect_category_t category;
    int effect_id;
    uint8_t brightness;
    uint8_t speed;
    uint8_t color[3];
} f87d_effect_manager_t;

void f87d_effmgr_init(f87d_effect_manager_t *mgr);
void f87d_effmgr_destroy(f87d_effect_manager_t *mgr);

int f87d_effmgr_set_hw(f87d_effect_manager_t *mgr, f87_device *dev,
                        int effect_id, uint8_t brightness, uint8_t speed,
                        uint8_t colorful, uint8_t r, uint8_t g, uint8_t b);

int f87d_effmgr_set_sw(f87d_effect_manager_t *mgr, f87_device *dev,
                        int effect_id, uint8_t brightness, uint8_t speed,
                        uint8_t r, uint8_t g, uint8_t b, int fps);

int f87d_effmgr_set_music(f87d_effect_manager_t *mgr, f87_device *dev,
                           int effect_id, uint8_t brightness,
                           uint8_t r, uint8_t g, uint8_t b, double gain);

int f87d_effmgr_set_sensor(f87d_effect_manager_t *mgr, f87_device *dev,
                            const char *profile, const char *config_path);

int f87d_effmgr_stop(f87d_effect_manager_t *mgr);

const char *f87d_effmgr_category_str(f87d_effect_category_t cat);
bool f87d_effmgr_has_sw_running(const f87d_effect_manager_t *mgr);

#endif
