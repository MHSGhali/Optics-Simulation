/* env.h — the sky: light arriving from every direction at once.
 *
 * WHAT THIS IS FOR
 *   A placed lamp answers "where is the light coming from". A uniform dome
 *   answers "what does this look like with no lighting decision at all" --
 *   an overcast sky, a lightbox, a subject inside an integrating sphere.
 *   Every surface that can see the sky is lit by it, so nothing is in
 *   shadow and nothing has a terminator, which is exactly what makes it the
 *   right background for judging FOCUS rather than lighting. Shadow contrast
 *   reads as sharpness to the eye, and that has already misled a reading of
 *   this program's own output once.
 *
 * THE INVARIANT THIS MODULE OWNS
 *   The dome is INFINITELY FAR AND UNOCCLUDED BY ITSELF. A ray that escapes
 *   the scene sees it; nothing else does. It has no geometry, so it is not a
 *   Prim, it is not in Scene.lights, and it can never be hit -- which is why
 *   it has to be a separate sampling strategy in the estimator rather than
 *   one more entry in the light array.
 *
 * WHY NOT A HUGE EMISSIVE SPHERE
 *   Because that is the same picture with worse numbers. A sphere large
 *   enough to read as a sky is nearly all outside any shading point's
 *   hemisphere, so light sampling wastes most of its samples, and the
 *   camera can focus on it. An escaped-ray lookup costs nothing and has no
 *   distance at all.
 *
 * RADIANCE, NOT ILLUMINANCE
 *   Stored as radiance, W/(m^2 sr nm), like every other emitted quantity in
 *   this program. It is AUTHORED as the illuminance a surface facing the
 *   dome receives, because that is the number a light meter reads and a
 *   photographer thinks in; os_scenedesc_build does that conversion, in the
 *   one place lights are authored, and divides by pi -- a uniform dome of
 *   radiance L puts pi*L on a surface below it.
 */
#ifndef OPTICSIM_ENV_H
#define OPTICSIM_ENV_H

#include "lightsim/spectrum.h"
#include "lightsim/vec.h"

typedef struct {
    bool     on;
    Spectrum le;      /* uniform radiance, W/(m^2 sr nm) */
} OsEnv;

/* Radiance arriving from direction `d`, at one wavelength.
 *
 * `d` is unused while the dome is uniform. It is a parameter anyway because
 * it is the seam a gradient sky or an imported HDRI arrives at, and a caller
 * that already passes the direction needs no edit when one does. */
static inline ls_real os_env_radiance(const OsEnv *e, vec3 d, ls_real lambda_nm) {
    (void)d;
    return (e && e->on) ? ls_spectrum_at(&e->le, lambda_nm) : 0.0;
}

#endif /* OPTICSIM_ENV_H */
