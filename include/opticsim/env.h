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
 * IT LIGHTS THE SCENE AND IS NOT PHOTOGRAPHED
 *   With one exception to the above: the CAMERA ray does not see it. A ray
 *   that leaves the film, misses everything and escapes returns black, while
 *   every ray after the first still collects the dome in full.
 *
 *   The reason is that a dome bright enough to light a scene is far brighter
 *   than anything it lights. A Lambertian surface returns rho times what falls
 *   on it and rho is below one, so with the sky in shot every subject is a
 *   silhouette against a blown white field -- and worse, that field is most of
 *   the frame, so it drives the exposure and there is no setting at which the
 *   subjects are correctly exposed AND the background is not clipped. The
 *   backdrop went for the same reason a wall did.
 *
 *   Nothing about the LIGHTING changes. Next-event estimation toward the dome
 *   is untouched, every indirect bounce still ends on it, and a surface under
 *   the sky returns exactly what it did before -- which the tests check
 *   against the closed form rather than against the picture. What changes is
 *   only what the camera records when it is pointed at nothing, and pointed at
 *   nothing a camera should record nothing.
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
