/* lensplot.c — surface profiles and traced ray fans, in millimetres. */
#include "lensplot.h"

#include <math.h>
#include <string.h>

/* The meridional profile of one surface: the arc x -> z(x) over its clear
 * aperture. For a sphere of radius R with its vertex at z_v,
 *
 *     z(x) = z_v + R - sign(R) sqrt(R^2 - x^2)
 *
 * which is the sag equation. A plano surface is a straight line. The sqrt
 * argument goes negative when the clear aperture exceeds the radius -- a
 * strongly curved element whose bore is wider than its own sphere -- and the
 * clamp below keeps that from producing a NaN that silently drops the whole
 * element out of the picture. */
static void profile(LpProfile *p, ls_real radius, ls_real semi_ap, ls_real z_v,
                    bool is_stop) {
    p->n = LP_PROFILE_PTS;
    p->is_stop = is_stop;
    for (int i = 0; i < LP_PROFILE_PTS; ++i) {
        ls_real x = -semi_ap + 2.0 * semi_ap * (ls_real)i
                             / (ls_real)(LP_PROFILE_PTS - 1);
        p->x[i] = x;
        if (fabs(radius) < 1e-12) {
            p->z[i] = z_v;
        } else {
            ls_real k = radius * radius - x * x;
            if (k < 0.0) k = 0.0;
            p->z[i] = z_v + radius - copysign(sqrt(k), radius);
        }
    }
}

/* Where a ray's final segment crosses the optical axis, measured from the rear
 * vertex. This is the REAL focus of that ray, and the spread of it across the
 * fan is exactly the aberration the diagram exists to show. */
static ls_real axis_crossing(const LpRay *r, ls_real rear_z) {
    if (r->n < 2) return HUGE_VAL;
    ls_real x0 = r->x[r->n - 2], z0 = r->z[r->n - 2];
    ls_real x1 = r->x[r->n - 1], z1 = r->z[r->n - 1];
    ls_real dx = x1 - x0;
    if (fabs(dx) < 1e-15) return HUGE_VAL;      /* parallel to the axis */
    ls_real t = -x0 / dx;
    return (z0 + t * (z1 - z0)) - rear_z;
}

void lp_build(LensPlot *lp, const OsLens *L, int nrays, bool chromatic,
              ls_real object_distance_m) {
    memset(lp, 0, sizeof *lp);
    if (nrays > LP_MAX_RAYS) nrays = LP_MAX_RAYS;
    if (nrays < 2) nrays = 2;

    ls_real rear_z = os_lens_vertex_z(L, L->nsurf - 1);
    lp->rear_z = rear_z;
    lp->film_z = rear_z + L->film_z_mm;
    lp->paraxial_focus_z = rear_z + L->bfd_mm;

    /* ---- the glass ---- */
    lp->nprofiles = L->nsurf;
    ls_real x_max = 0.0;
    for (int i = 0; i < L->nsurf; ++i) {
        bool is_stop = (i == L->stop_index);
        ls_real semi = is_stop ? L->stop_semi_ap_mm : L->surf[i].semi_ap_mm;
        profile(&lp->profile[i], L->surf[i].radius_mm, semi,
                os_lens_vertex_z(L, i), is_stop);
        /* Glass between this surface and the next means the pair bounds a
         * physical element. Comparing the index to 1 rather than comparing
         * glass ids keeps this working for model and constant glasses too. */
        lp->profile[i].solid_after =
            (i + 1 < L->nsurf) && (os_glass_n(&L->glass[i], OS_LINE_D) > 1.0 + 1e-9);
        if (semi > x_max) x_max = semi;
        if (is_stop) {
            lp->stop_z = os_lens_vertex_z(L, i);
            lp->stop_semi_ap = semi;
        }
    }

    /* ---- the light ---- */
    static const ls_real LAMBDAS[3] = { OS_LINE_F, OS_LINE_D, OS_LINE_C };
    int nlam = chromatic ? 3 : 1;
    const ls_real *lam = chromatic ? LAMBDAS : &LAMBDAS[1];

    /* Fan out to 1.15x the pupil, deliberately past the edge, so the rays that
     * get clipped are drawn being clipped. Vignetting you can see beats
     * vignetting you have to infer from an absence. */
    ls_real h_max = L->ep_semi_ap_mm * 1.15;
    ls_real start_z = -0.35 * (L->efl_mm > 0.0 ? L->efl_mm : 50.0);

    for (int k = 0; k < nlam; ++k) {
        for (int i = 0; i < nrays; ++i) {
            ls_real h = -h_max + 2.0 * h_max * (ls_real)i / (ls_real)(nrays - 1);

            OsLensRay start;
            if (isinf(object_distance_m)) {
                /* Parallel to the axis: the classic infinity-focus fan, where
                 * every ray should meet at one point if the lens were perfect. */
                start.o = v3(h, 0.0, start_z);
                start.d = v3(0.0, 0.0, 1.0);
            } else {
                /* From a point on the axis, aimed across the entrance pupil. */
                ls_real z0 = -object_distance_m * 1000.0;
                vec3 o = v3(0.0, 0.0, z0);
                vec3 target = v3(h, 0.0, L->ep_z_mm);
                start.o = o;
                start.d = v3norm(v3sub(target, o));
                /* Start the drawn segment near the lens rather than metres
                 * away, or the whole diagram scales to the object distance and
                 * the lens becomes a speck. */
                ls_real t = (start_z - z0) / start.d.z;
                start.o = v3add(o, v3scale(start.d, t));
            }

            OsRayPath path;
            bool ok = os_lens_trace_path(L, lam[k], start, lp->film_z, &path);

            LpRay *r = &lp->ray[lp->nrays++];
            r->n = path.n < OS_PATH_MAX ? path.n : OS_PATH_MAX;
            r->blocked = !ok;
            r->lambda_nm = lam[k];
            for (int j = 0; j < r->n; ++j) { r->x[j] = path.p[j].x; r->z[j] = path.p[j].z; }
            r->cross_z = ok ? axis_crossing(r, rear_z) : HUGE_VAL;

            for (int j = 0; j < r->n; ++j)
                if (fabs(r->x[j]) > x_max) x_max = fabs(r->x[j]);
        }
    }

    lp->z_min = start_z;
    lp->z_max = lp->film_z > lp->paraxial_focus_z ? lp->film_z : lp->paraxial_focus_z;
    lp->z_max += 0.06 * (lp->z_max - lp->z_min);
    lp->x_max = x_max * 1.12;
    if (lp->x_max <= 0.0) lp->x_max = 1.0;
}
