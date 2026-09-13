/* pupil.h — where on the rear element a sensor point can usefully aim.
 *
 * THE INVARIANT THIS MODULE OWNS
 *   The cached bound always CONTAINS the true exit pupil for every sensor point
 *   in its zone. A loose bound costs only speed. A tight bound silently deletes
 *   light from the frame's corners -- and the result looks exactly like
 *   tasteful vignetting, which is why nobody would ever find it by looking.
 *
 * WHY IT EXISTS
 *   The naive approach samples the whole rear element uniformly. On axis that
 *   is fine; toward the corners most of the element is not reachable through
 *   the rest of the glass, and upwards of 90 % of rays are traced only to be
 *   thrown away at some interior surface.
 *
 *   The lens is rotationally symmetric, so the reachable region depends only on
 *   the sensor point's RADIUS, and its azimuth just rotates the answer. Hence a
 *   table over radial zones, each holding an axis-aligned box, built once and
 *   rotated at sample time.
 *
 * SAMPLING STAYS UNBIASED
 *   Points are drawn uniformly inside the box and weighted by its area, so the
 *   estimator is unbiased for ANY box that contains the true pupil. Rays that
 *   fall inside the box but miss the real pupil are traced and rejected, which
 *   costs time and no accuracy. That asymmetry is why the build pads.
 */
#ifndef OPTICSIM_PUPIL_H
#define OPTICSIM_PUPIL_H

#include "opticsim/lens.h"

typedef struct { ls_real x0, x1, y0, y1; } OsRect;

typedef struct {
    OsRect *zone;            /* one per radial zone, on the rear vertex plane */
    int     nzones;          /* 0 with a rear_semi_mm = the TRIVIAL bound     */
    ls_real film_radius_mm;  /* the largest sensor radius covered             */
    ls_real rear_z_mm;       /* rear vertex, from the front one               */
    ls_real rear_semi_mm;    /* the rear element, which is the loosest bound  */
} OsPupilCache;

/* Build the table for this lens as currently scaled, stopped and focused.
 * Costs a few hundred thousand lens traces -- milliseconds -- so it belongs
 * outside any render loop, once per control change. */
bool os_pupil_build(OsPupilCache *c, const OsLens *L, ls_real film_radius_mm,
                    int nzones);

/* The cache in its LOOSEST valid state: no zones, no traces, and every sensor
 * point bounded by the whole rear element. That is the naive sampler this
 * module was written to replace -- correct, unbiased, and slower.
 *
 * It exists so a camera is usable the moment it is built without paying for a
 * table that its caller is about to invalidate. os_camera_build used to run the
 * full scan, roughly 74 000 lens traces, and then every caller changed the
 * focus and ran it again; the first one was pure waste, and simply dropping it
 * would have left a camera whose every ray reported itself vignetted -- a
 * BLACK frame, which is a worse failure than a slow one.
 *
 * Costs nothing. Call os_pupil_build when the lens has stopped moving. */
void os_pupil_init_trivial(OsPupilCache *c, const OsLens *L);

void os_pupil_free(OsPupilCache *c);

/* The bound for a sensor point at radius `r_mm`, unrotated. */
OsRect os_pupil_bounds(const OsPupilCache *c, ls_real r_mm);

/* Sample a point on the rear plane for a sensor point at (fx, fy) mm, and
 * report the area sampled over so the caller can weight by it. Returns false
 * only if the cache is empty. */
bool os_pupil_sample(const OsPupilCache *c, ls_real fx_mm, ls_real fy_mm,
                     ls_real u1, ls_real u2,
                     ls_real *rx_mm, ls_real *ry_mm, ls_real *area_mm2);

#endif /* OPTICSIM_PUPIL_H */
