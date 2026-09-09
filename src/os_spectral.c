/* os_spectral.c — the hero-wavelength draw. See spectral.h for why there is
 * only one wavelength per path. */
#include "opticsim/spectral.h"

#include <math.h>

uint32_t os_pixel_hash(int x, int y) {
    /* A 32-bit integer hash (Wang/Jenkins style): two large odd multipliers and
     * an xor-shift finaliser. It only has to decorrelate neighbouring pixels,
     * so quality matters more than speed here, and the obvious `x * 73856093 ^
     * y * 19349663` leaves visible diagonal structure at low sample counts. */
    uint32_t h = (uint32_t)x * 0x9E3779B1u ^ (uint32_t)y * 0x85EBCA77u;
    h ^= h >> 15; h *= 0x2C1B3C6Du;
    h ^= h >> 12; h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

OsWavelength os_lambda_pick(uint64_t sample_index, uint32_t pixel_hash) {
    /* Additive recurrence with the golden ratio's fractional part. Successive
     * samples land in the largest remaining gap, so any prefix of the sequence
     * covers the band about as evenly as a prefix can -- and it needs no state,
     * which is what lets a pass be restarted or split across threads without
     * changing the result. */
    const ls_real PHI = 0.6180339887498949;
    ls_real offset = (ls_real)(pixel_hash & 0xFFFFFFu) / (ls_real)0x1000000u;
    ls_real u = fmod((ls_real)sample_index * PHI + offset, 1.0);
    if (u < 0.0) u += 1.0;

    int bin = (int)(u * (ls_real)LS_NBINS);
    /* u can reach 1.0 through rounding; clamping is cheaper than proving it
     * cannot, and an out-of-range bin here would be an out-of-bounds write. */
    if (bin < 0) bin = 0;
    if (bin >= LS_NBINS) bin = LS_NBINS - 1;

    OsWavelength w;
    w.bin       = bin;
    w.lambda_nm = ls_bin_lambda(bin);
    /* Uniform over bins for now. When the sensor lands, this becomes the
     * photosite's own response and inv_pdf stops being a constant -- which is
     * why callers must use this field rather than assuming LS_NBINS. */
    w.inv_pdf   = (ls_real)LS_NBINS;
    return w;
}
