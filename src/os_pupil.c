/* os_pupil.c — the exit-pupil bounds cache. See pupil.h for the invariant. */
#include "opticsim/pupil.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GRID 48          /* probes per axis, per zone */
#define PAD_CELLS 2.0    /* how far the found box is inflated, in grid cells */

void os_pupil_free(OsPupilCache *c) {
    free(c->zone);
    c->zone = NULL;
    c->nzones = 0;
    c->rear_semi_mm = 0.0;
}

void os_pupil_init_trivial(OsPupilCache *c, const OsLens *L) {
    free(c->zone);
    c->zone = NULL;
    c->nzones = 0;
    c->film_radius_mm = 0.0;
    c->rear_z_mm    = os_lens_vertex_z(L, L->nsurf - 1);
    c->rear_semi_mm = L->surf[L->nsurf - 1].semi_ap_mm;
}

bool os_pupil_build(OsPupilCache *c, const OsLens *L, ls_real film_radius_mm,
                    int nzones) {
    if (nzones < 2) nzones = 2;
    c->zone = calloc((size_t)nzones, sizeof *c->zone);
    if (!c->zone) return false;
    c->nzones = nzones;
    c->film_radius_mm = film_radius_mm;
    c->rear_z_mm = os_lens_vertex_z(L, L->nsurf - 1);
    c->rear_semi_mm = L->surf[L->nsurf - 1].semi_ap_mm;

    ls_real film_z = os_lens_film_z(L);
    ls_real rear_semi = c->rear_semi_mm;

    for (int z = 0; z < nzones; ++z) {
        /* One representative sensor point per zone, on the +x axis. Rotational
         * symmetry means every other azimuth is this answer, rotated. */
        ls_real fr = film_radius_mm * (ls_real)z / (ls_real)(nzones - 1);
        vec3 film = v3(fr, 0.0, film_z);

        ls_real lo_x = HUGE_VAL, hi_x = -HUGE_VAL;
        ls_real lo_y = HUGE_VAL, hi_y = -HUGE_VAL;
        bool any = false;

        for (int iy = 0; iy < GRID; ++iy) {
            ls_real ry = -rear_semi + 2.0 * rear_semi * ((ls_real)iy + 0.5) / (ls_real)GRID;
            for (int ix = 0; ix < GRID; ++ix) {
                ls_real rx = -rear_semi + 2.0 * rear_semi * ((ls_real)ix + 0.5) / (ls_real)GRID;
                if (rx * rx + ry * ry > rear_semi * rear_semi) continue;

                vec3 target = v3(rx, ry, c->rear_z_mm);
                OsLensRay r = { film, v3norm(v3sub(target, film)) };
                /* Probed at the d line. The pupil moves by microns across the
                 * band, which the padding below absorbs; the superset property
                 * is asserted at 400, 550 and 700 nm in the tests. */
                if (!os_lens_trace_reverse(L, OS_LINE_D, &r, NULL)) continue;

                any = true;
                if (rx < lo_x) lo_x = rx;
                if (rx > hi_x) hi_x = rx;
                if (ry < lo_y) lo_y = ry;
                if (ry > hi_y) hi_y = ry;
            }
        }

        if (!any) {
            /* Nothing got through from this sensor radius -- it is outside the
             * image circle. An empty box would be a zero-area sample, so the
             * zone is marked degenerate and os_pupil_sample reports zero area,
             * which is the truthful answer: no light reaches there. */
            c->zone[z] = (OsRect){ 0.0, 0.0, 0.0, 0.0 };
            continue;
        }

        /* THE padding. The grid finds the pupil only to within one cell, so an
         * unpadded box is systematically too small -- and a too-small box
         * darkens the frame edges in a way indistinguishable from real
         * vignetting. Two cells is cheap insurance; the cost is a few percent
         * more rejected rays. */
        ls_real cell = 2.0 * rear_semi / (ls_real)GRID;
        ls_real pad = PAD_CELLS * cell;
        c->zone[z] = (OsRect){ lo_x - pad, hi_x + pad, lo_y - pad, hi_y + pad };
    }
    return true;
}

OsRect os_pupil_bounds(const OsPupilCache *c, ls_real r_mm) {
    if (c->nzones <= 0) {
        /* No table: fall back to the whole rear element, which is the loosest
         * bound that still CONTAINS the pupil and therefore still satisfies
         * this module's invariant. Slower, never darker. A cache that has not
         * even been given a lens has no rear element to name and gets the
         * empty box, which is the only honest answer to a question about a
         * lens that is not there. */
        ls_real h = c->rear_semi_mm;
        return (OsRect){ -h, h, -h, h };
    }
    ls_real t = (c->film_radius_mm > 0.0) ? r_mm / c->film_radius_mm : 0.0;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;

    /* Take the UNION of the two bracketing zones rather than interpolating
     * between them. Interpolation can produce a box narrower than either
     * neighbour where the pupil is changing shape quickly, which breaks the
     * superset invariant exactly where it matters most. A union cannot. */
    ls_real f = t * (ls_real)(c->nzones - 1);
    int i = (int)f;
    if (i >= c->nzones - 1) i = c->nzones - 2;
    OsRect a = c->zone[i], b = c->zone[i + 1];

    OsRect r;
    r.x0 = a.x0 < b.x0 ? a.x0 : b.x0;
    r.x1 = a.x1 > b.x1 ? a.x1 : b.x1;
    r.y0 = a.y0 < b.y0 ? a.y0 : b.y0;
    r.y1 = a.y1 > b.y1 ? a.y1 : b.y1;
    return r;
}

bool os_pupil_sample(const OsPupilCache *c, ls_real fx_mm, ls_real fy_mm,
                     ls_real u1, ls_real u2,
                     ls_real *rx_mm, ls_real *ry_mm, ls_real *area_mm2) {
    if (c->nzones <= 0 && !(c->rear_semi_mm > 0.0)) return false;

    ls_real r = sqrt(fx_mm * fx_mm + fy_mm * fy_mm);
    OsRect b = os_pupil_bounds(c, r);

    ls_real w = b.x1 - b.x0, h = b.y1 - b.y0;
    if (!(w > 0.0) || !(h > 0.0)) { *area_mm2 = 0.0; return false; }

    ls_real px = b.x0 + w * u1;
    ls_real py = b.y0 + h * u2;

    /* The box was found for a sensor point on the +x axis. Rotate it to this
     * point's actual azimuth -- that is the whole payoff of the lens being
     * rotationally symmetric. */
    if (r > 1e-12) {
        ls_real ca = fx_mm / r, sa = fy_mm / r;
        *rx_mm = px * ca - py * sa;
        *ry_mm = px * sa + py * ca;
    } else {
        *rx_mm = px;
        *ry_mm = py;
    }
    *area_mm2 = w * h;
    return true;
}
