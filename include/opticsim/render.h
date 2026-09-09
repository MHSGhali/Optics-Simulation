/* render.h — accumulate camera samples into a film.
 *
 * THE INVARIANT THIS MODULE OWNS
 *   Row y of pass p produces the same numbers at one thread and at sixty-four.
 *
 *   The vendored ls_parallel_for seeds its RNG from the WORK-ITEM INDEX, never
 *   from a thread id, and that is the whole reason. Seeding per thread makes
 *   every render depend on scheduling, so a bug reproduces only sometimes and a
 *   regression test cannot exist at all. The property is defended by a test
 *   that memcmps the film at 1 and 8 threads.
 *
 * WHERE NOISE DOES NOT GO
 *   The film holds NOISE-FREE spectral irradiance. Sensor noise -- shot, read,
 *   quantisation -- belongs at development, applied once, and does not live
 *   here. Applying it per Monte Carlo sample would average it away as the
 *   sample count rose, so the ISO control would visibly stop doing anything at
 *   high quality. A real photograph is one readout, not the mean of a thousand.
 */
#ifndef OPTICSIM_RENDER_H
#define OPTICSIM_RENDER_H

#include "opticsim/camera.h"
#include "opticsim/stage.h"
#include "lightsim/film.h"
#include "lightsim/scene.h"

typedef struct {
    int  spp;            /* samples per pixel per pass */
    int  max_depth;
    int  threads;        /* 0 = one per core */
    uint64_t seed;
} OsRenderOpts;

/* Accumulate one pass into `film`. Call repeatedly to refine; the film's own
 * per-pixel counter carries the running mean, so passes simply add.
 *
 * Takes the STAGE, not the bare Scene, because the environment dome is light
 * with no geometry: it is not a Prim and not an entry in Scene.lights, so a
 * caller handing over `&stage.scene` would render an unlit picture and no
 * signature would have objected. Scene and sky travel together or not at
 * all. */
void os_render_pass(Film *film, const OsCamera *cam, const OsStage *st,
                    const OsRenderOpts *opt, int pass);

#endif /* OPTICSIM_RENDER_H */
