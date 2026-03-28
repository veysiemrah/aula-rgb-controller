#include "animate_internal.h"

/* Reactive effects — implemented in Task 11 */

static const f87_sw_effect_t *reactive_effects[] = {
    NULL
};

const f87_sw_effect_t *f87_reactive_find_effect(f87_sw_effect_id id)
{
    for (int i = 0; reactive_effects[i] != NULL; i++) {
        if (reactive_effects[i]->id == id)
            return reactive_effects[i];
    }
    return NULL;
}
