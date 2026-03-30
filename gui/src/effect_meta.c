#include "effect_meta.h"
#include "i18n.h"
#include <stddef.h>

#define B  F87_PARAM_BRIGHTNESS
#define S  F87_PARAM_SPEED
#define C  F87_PARAM_COLOR
#define CF F87_PARAM_COLORFUL
#define A  F87_PARAM_AUDIO
#define P  F87_PARAM_PROFILE
#define PT F87_PARAM_PAINT

static const effect_meta_t meta_table[] = {
    /* HW effects */
    {  0, 0,              NULL,      N_("Turn off all LEDs") },
    {  1, B | C,          NULL,      N_("Single solid color") },
    {  2, B|S|C|CF,       NULL,      N_("Pulsing color fade") },
    {  3, B | S,          "rainbow", N_("Rainbow wave across keyboard") },
    {  4, B|S|C|CF,       NULL,      N_("Color spread from key presses") },
    {  5, B|S|C|CF,       NULL,      N_("Falling rain drops") },
    {  7, B|S|C|CF,       NULL,      N_("Ripple waves from key presses") },
    {  8, B|S|C|CF,       NULL,      N_("Random twinkling stars") },
    { 10, B|S|C|CF,       NULL,      N_("Snake trail moving across keys") },
    { 11, B|S|C|CF,       NULL,      N_("Northern lights shimmer") },
    { 12, B|S|C|CF,       NULL,      N_("Single key lights up on press") },
    { 13, B|S|C|CF,       NULL,      N_("Scrolling light marquee") },
    { 15, B | S,          "rainbow", N_("Circular rainbow pattern") },
    { 16, B | S,          "rainbow", N_("Top to bottom wave") },
    { 17, B | S,          "rainbow", N_("Ripple from center outward") },
    { 18, B | C | PT,     NULL,      N_("Paint each key individually") },

    /* SW effects */
    {100, B | S | C,      NULL,      N_("Doom fire algorithm") },
    {101, B | S | C,      NULL,      N_("Matrix digital rain") },
    {102, B | S,          "rainbow", N_("Colorful plasma waves") },
    {103, B,              NULL,      N_("CPU temperature heatmap") },
    {104, B | S | C,      NULL,      N_("Rotating radar sweep") },
    {105, B | S | C,      NULL,      N_("Random lightning bolts") },
    {110, B|S|C|CF,       NULL,      N_("Colorful explosions on key press") },
    {111, B | S | C,      NULL,      N_("Software ripple waves on key press") },
    {112, B | S,          NULL,      N_("Heat trail on key press") },
    {113, B | S | C,      NULL,      N_("Conway's Game of Life on key press") },
    {114, B | S,          NULL,      N_("Cumulative key usage heatmap") },

    /* Music effects */
    {200, B | S | A,      NULL,      N_("Audio spectrum analyzer bars") },
    {201, B | A,          NULL,      N_("Flash on beat detection") },
    {202, B|S|C|A,        NULL,      N_("Expanding energy waves from audio") },
    {203, B | S | A,      NULL,      N_("Classic VU meter display") },
    {204, B | S | A,      NULL,      N_("Frequency band mapping") },

    /* Sensor */
    {106, B | P,          NULL,      N_("Developer sensor profile") },
    {107, B | P,          NULL,      N_("Gamer sensor profile") },
    {108, B | P,          NULL,      N_("System monitor profile") },
};

#undef B
#undef S
#undef C
#undef CF
#undef A
#undef P
#undef PT

static const effect_meta_t default_meta = { -1, F87_PARAM_BRIGHTNESS, NULL, NULL };

#define META_COUNT (sizeof(meta_table) / sizeof(meta_table[0]))

const effect_meta_t *effect_meta_lookup(int effect_id)
{
    for (unsigned i = 0; i < META_COUNT; i++) {
        if (meta_table[i].effect_id == effect_id)
            return &meta_table[i];
    }
    return &default_meta;
}
