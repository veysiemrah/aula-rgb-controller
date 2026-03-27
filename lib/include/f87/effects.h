#ifndef F87_EFFECTS_H
#define F87_EFFECTS_H

#include "device.h"
#include "lighting.h"
#include <stdint.h>

/*
 * Lighting modes.
 *
 * The keyboard stores hardware effects in its flash (static, breathing,
 * wave, etc.), but the protocol for selecting them is not yet fully
 * reverse-engineered.  For now we expose only the two modes we can
 * drive reliably via the direct-LED HID feature report:
 *
 *   MODE_OFF    — all LEDs black
 *   MODE_DIRECT — per-key RGB via f87_apply() / f87_set_key_color()
 *
 * The old hardware-effect IDs from KB.ini are preserved in comments
 * for future use once the effect-selection command is understood.
 */

/*
 * Hardware effect IDs extracted from KB.ini (LedOpt entries).
 * These are NOT usable yet — kept here for reference.
 *
 *   HW  1 = Static           HW 13 = Reactive
 *   HW  2 = Wave (rainbow)   HW 14 = Cycle
 *   HW  3 = Breathing        HW 15 = Ripple
 *   HW  5 = Marquee          HW 16 = Rain
 *   HW  7 = Aurora           HW 17 = Laser
 *   HW  8 = Gradient         HW 18 = Snake
 *   HW 12 = Firework         HW 19 = Spectrum
 *   HW 20 = Starlight        HW 28 = Rainbow Wave
 *   HW 29 = Tidal            HW 30 = Prism
 *   HW  0 = Custom (per-key)
 */

typedef enum {
    F87_MODE_OFF    = 0,   /* all LEDs off                           */
    F87_MODE_DIRECT = 1,   /* per-key RGB via direct LED reports     */
    F87_MODE_COUNT  = 2
} f87_mode;

typedef enum {
    F87_DIR_RIGHT = 0,
    F87_DIR_LEFT,
    F87_DIR_UP,
    F87_DIR_DOWN
} f87_direction;

typedef struct {
    f87_mode mode;
    uint8_t speed;          /* 0-10  (reserved for future hw effects) */
    uint8_t brightness;     /* 0-100 */
    f87_color color1;
    f87_color color2;
    f87_direction direction; /* reserved for future hw effects */
} f87_effect;

int f87_set_effect(f87_device *dev, const f87_effect *effect);
int f87_get_current_effect(f87_device *dev, f87_effect *effect);

const char *f87_mode_name(f87_mode mode);

#endif
