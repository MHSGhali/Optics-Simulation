/* os_render.c — the render loop. See render.h for the determinism contract. */
#include "opticsim/render.h"
#include "opticsim/trace.h"
#include "lightsim/thread.h"

#include <string.h>

typedef struct {
    Film             *film;
    const OsCamera   *cam;
    const Scene      *sc;
    const OsEnv      *env;
    const OsRenderOpts *opt;
    int               pass;
} RowJob;

static void render_row(int y, void *user) {
    RowJob *j = user;
    const OsCamera *cam = j->cam;
    int W = cam->width;
    int spp = j->opt->spp;

    for (int x = 0; x < W; ++x) {
        /* Seeded from the PIXEL and the PASS, never from the thread or from a
         * running counter. That is what makes the result identical at any
         * thread count, and it survives a pass being re-run.
         *
         * THE WIDENING IS LOAD-BEARING, and it was in the wrong place. The
         * multiply used to happen in `int` -- `(y * W + x) * 977` with every
         * term an int -- and only the finished value was widened. That
         * overflows above INT_MAX/977, about 2.2 megapixels, so any render past
         * roughly 2048x1152 was signed overflow: undefined behaviour, and in
         * practice a wrap that lands distinct pixels on the SAME stream. Two
         * pixels drawing the same pupil positions, the same BSDF directions and
         * the same light choices is correlated noise that no number of passes
         * averages away, and it breaks the determinism contract in render.h in
         * a way that comparing two thread counts cannot see -- both threadings
         * are wrong identically. --width and --height come straight from atoi
         * with no bound, so it was reachable from the command line.
         *
         * THE STRIDE has to outrun the pass count, too. At 977 the stream index
         * of pixel k at pass 977 was 977(k+1) + 1, which is exactly pixel k+1's
         * stream at pass 0 -- so once a render passed 977 iterations, horizontal
         * neighbours started replaying each other's sequences. The viewer's pass
         * counter is unbounded, so a window left open on one scene reached it in
         * minutes and then quietly stopped converging cleanly. A prime past a
         * million puts that beyond any render anyone will sit through. */
        Rng rng = ls_rng_seed(j->opt->seed,
                              ((uint64_t)y * (uint64_t)W + (uint64_t)x)
                              * 1000003ull + (uint64_t)j->pass + 1);

        for (int s = 0; s < spp; ++s) {
            /* The sample index spans passes, so the wavelength stratification
             * keeps filling in as a render refines rather than repeating the
             * same few wavelengths every pass. */
            uint64_t sample_index = (uint64_t)j->pass * (uint64_t)spp + (uint64_t)s;

            OsCameraSample cs;
            Spectrum dep = ls_spectrum_zero();

            if (os_camera_sample(cam, x, y, sample_index, &rng, &cs)) {
                ls_real L = os_trace_radiance(j->sc, j->env, cs.ray,
                                              cs.lambda_nm, &rng,
                                              j->opt->max_depth);
                /* Deposit into the ONE bin this path sampled, weighted by the
                 * reciprocal of the probability of having sampled it. Over many
                 * samples each bin receives an unbiased estimate of the
                 * radiance at its own wavelength. */
                dep.v[cs.bin] = (float)(cs.weight * L);
            }
            /* A vignetted sample still lands here, contributing zero. Skipping
             * it instead would renormalise the vignetting away and make the
             * corners exactly as bright as the centre. */
            ls_film_add(j->film, x, y, &dep);
        }
    }
}

void os_render_pass(Film *film, const OsCamera *cam, const OsStage *st,
                    const OsRenderOpts *opt, int pass) {
    RowJob job = { film, cam, &st->scene, &st->env, opt, pass };
    ls_parallel_for(cam->height, opt->threads, render_row, &job);
}
