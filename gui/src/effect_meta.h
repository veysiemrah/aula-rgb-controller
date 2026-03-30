#ifndef F87_EFFECT_META_H
#define F87_EFFECT_META_H

#include <stdint.h>

typedef enum {
    F87_PARAM_BRIGHTNESS = (1 << 0),
    F87_PARAM_SPEED      = (1 << 1),
    F87_PARAM_COLOR      = (1 << 2),
    F87_PARAM_COLORFUL   = (1 << 3),
    F87_PARAM_AUDIO      = (1 << 4),
    F87_PARAM_PROFILE    = (1 << 5),
    F87_PARAM_PAINT      = (1 << 6),
} f87_param_flags;

typedef struct {
    int effect_id;
    uint32_t flags;
    const char *tag;   /* "rainbow", NULL, etc. — not translated */
    const char *desc;  /* N_() marked description — translated at display time */
} effect_meta_t;

/* Returns metadata for given effect_id. Never returns NULL — unknown IDs
   return a default entry with only BRIGHTNESS flag. */
const effect_meta_t *effect_meta_lookup(int effect_id);

#endif /* F87_EFFECT_META_H */
