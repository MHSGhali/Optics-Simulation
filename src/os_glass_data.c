/* os_glass_data.c — the Schott catalogue entries, and nothing else.
 *
 * Kept apart from os_glass.c so that the arithmetic and the data can be
 * reviewed separately. The arithmetic is nine lines and provable; the data is
 * transcription, which is where the errors actually are. Every row carries its
 * published n_d and V_d, and os_glass_self_check() recomputes both from the
 * coefficients -- so a mistyped digit fails the build rather than quietly
 * changing a lens's colour correction.
 *
 * Sorted by n_d, crowns (high V_d) first then flints (low V_d), which is the
 * order a lens designer thinks in: the achromat pairs a crown with a flint.
 */
#include "opticsim/glass.h"

#include <stddef.h>

static const OsGlass CATALOGUE[OS_GLASS_COUNT] = {
    /* Air. All B zero, so n^2 = 1 exactly at every wavelength -- no rounding,
     * no dispersion. The published fields are 0 to mark "nothing to check". */
    [OS_GLASS_AIR] = {
        .B = { 0.0, 0.0, 0.0 },
        .C = { 0.0, 0.0, 0.0 },
        .nd_published = 0.0, .vd_published = 0.0, .name = "air"
    },

    /* ---- crowns ---- */
    [OS_GLASS_N_BK7] = {
        .B = { 1.03961212, 0.231792344, 1.01046945 },
        .C = { 0.00600069867, 0.0200179144, 103.560653 },
        .nd_published = 1.51680, .vd_published = 64.17, .name = "N-BK7"
    },
    [OS_GLASS_N_SK16] = {
        .B = { 1.34317774, 0.241144399, 0.994317969 },
        .C = { 0.00704687339, 0.0229005, 92.7508526 },
        .nd_published = 1.62041, .vd_published = 60.32, .name = "N-SK16"
    },
    [OS_GLASS_N_LAK9] = {
        .B = { 1.462312, 0.344399589, 1.15508372 },
        .C = { 0.00724270156, 0.0243353131, 85.4686868 },
        .nd_published = 1.69100, .vd_published = 54.71, .name = "N-LAK9"
    },
    [OS_GLASS_N_BAF10] = {
        .B = { 1.5851495, 0.143559385, 1.08521269 },
        .C = { 0.00926681282, 0.0424489805, 105.613573 },
        .nd_published = 1.67003, .vd_published = 47.11, .name = "N-BAF10"
    },
    [OS_GLASS_N_LAF2] = {
        .B = { 1.80984227, 0.15729555, 1.0930037 },
        .C = { 0.0101711622, 0.0442431765, 100.687748 },
        .nd_published = 1.74397, .vd_published = 44.85, .name = "N-LAF2"
    },

    /* ---- flints ---- */
    [OS_GLASS_F2] = {
        .B = { 1.34533359, 0.209073176, 0.937357162 },
        .C = { 0.00997743871, 0.0470450767, 111.886764 },
        .nd_published = 1.62004, .vd_published = 36.37, .name = "F2"
    },
    [OS_GLASS_SF2] = {
        .B = { 1.40301821, 0.231767504, 0.939056586 },
        .C = { 0.0105795466, 0.0493226978, 112.405955 },
        .nd_published = 1.64769, .vd_published = 33.85, .name = "SF2"
    },
    [OS_GLASS_N_SF5] = {
        .B = { 1.52481889, 0.187085527, 1.42729015 },
        .C = { 0.011254756, 0.0588995392, 129.141675 },
        .nd_published = 1.67271, .vd_published = 32.25, .name = "N-SF5"
    },
    [OS_GLASS_SF11] = {
        .B = { 1.73759695, 0.313747346, 1.89878101 },
        .C = { 0.013188707, 0.0623068142, 155.23629 },
        .nd_published = 1.78472, .vd_published = 25.68, .name = "SF11"
    },
};

const OsGlass *os_glass(OsGlassId id) {
    if (id < 0 || id >= OS_GLASS_COUNT) return &CATALOGUE[OS_GLASS_AIR];
    return &CATALOGUE[id];
}
