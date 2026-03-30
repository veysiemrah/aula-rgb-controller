#include "effect_meta.h"
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
    {  0, 0,                          NULL },       /* Off */
    {  1, B | C,                      NULL },       /* Static */
    {  2, B | S | C | CF,             NULL },       /* Breathing */
    {  3, B | S,                      "rainbow" },  /* Wave */
    {  4, B | S | C | CF,             NULL },       /* Spectrum */
    {  5, B | S | C | CF,             NULL },       /* Rain */
    {  7, B | S | C | CF,             NULL },       /* Ripple */
    {  8, B | S | C | CF,             NULL },       /* Starlight */
    { 10, B | S | C | CF,             NULL },       /* Snake */
    { 11, B | S | C | CF,             NULL },       /* Aurora */
    { 12, B | S | C | CF,             NULL },       /* Reactive */
    { 13, B | S | C | CF,             NULL },       /* Marquee */
    { 15, B | S,                      "rainbow" },  /* Circle */
    { 16, B | S,                      "rainbow" },  /* Rain Down */
    { 17, B | S,                      "rainbow" },  /* Center Ripple */
    { 18, B | C | PT,                 NULL },       /* Custom */

    /* SW effects */
    {100, B | S | C,                  NULL },       /* Fire */
    {101, B | S | C,                  NULL },       /* Matrix */
    {102, B | S,                      "rainbow" },  /* Plasma */
    {103, B,                          NULL },       /* Heatmap */
    {104, B | S | C,                  NULL },       /* Radar */
    {105, B | S | C,                  NULL },       /* Lightning */
    {110, B | S,                      "rainbow" },  /* Explode */
    {111, B | S | C,                  NULL },       /* Ripple SW */
    {112, B | S,                      NULL },       /* Typewriter */
    {113, B | S | C,                  NULL },       /* Life */
    {114, B | S,                      NULL },       /* KeyHeat */

    /* Music effects */
    {200, B | S | A,                  NULL },       /* Spectrum */
    {201, B | A,                      NULL },       /* Beat */
    {202, B | S | C | A,             NULL },       /* Energy */
    {203, B | S | A,                  NULL },       /* VU */
    {204, B | S | A,                  NULL },       /* FreqMap */

    /* Sensor */
    {106, B | P,                      NULL },       /* developer */
    {107, B | P,                      NULL },       /* gamer */
    {108, B | P,                      NULL },       /* system */
};

#undef B
#undef S
#undef C
#undef CF
#undef A
#undef P
#undef PT

static const effect_meta_t default_meta = { -1, F87_PARAM_BRIGHTNESS, NULL };

#define META_COUNT (sizeof(meta_table) / sizeof(meta_table[0]))

const effect_meta_t *effect_meta_lookup(int effect_id)
{
    for (unsigned i = 0; i < META_COUNT; i++) {
        if (meta_table[i].effect_id == effect_id)
            return &meta_table[i];
    }
    return &default_meta;
}
