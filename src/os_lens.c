/* os_lens.c — first-order (paraxial) analysis and the lens controls.
 *
 * See lens.h for the coordinate convention and for why the paraxial trace is
 * kept alongside the real one.
 */
#include "opticsim/lens.h"
#include "lightsim/bsdf.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

ls_real os_lens_n_after(const OsLens *L, int i, ls_real lambda_nm) {
    if (i < 0 || i >= L->nsurf) return 1.0;
    return os_glass_n(&L->glass[i], lambda_nm);
}

ls_real os_lens_n_before(const OsLens *L, int i, ls_real lambda_nm) {
    /* Object space, ahead of the first surface, is air by definition. */
    if (i <= 0) return 1.0;
    return os_glass_n(&L->glass[i - 1], lambda_nm);
}

ls_real os_lens_vertex_z(const OsLens *L, int i) {
    ls_real z = 0.0;
    for (int k = 0; k < i && k < L->nsurf; ++k) z += L->surf[k].thickness_mm;
    return z;
}

/* ---- the y-nu trace -----------------------------------------------------
 *
 * One paraxial ray, traced surface by surface. `y` is the height at the
 * current surface and `u` the slope leaving it. Refraction at a surface of
 * radius R between indices n and n' is
 *
 *     n' u' = n u - y (n' - n) / R
 *
 * and transfer to the next surface is y += u' t. A plano surface (R = 0) has
 * zero power, which the guard below expresses directly rather than by dividing
 * by zero and hoping the infinity cancels.
 *
 * Entering parallel to the axis at height 1 makes the outputs fall out:
 * the ray crosses the axis one focal length behind the rear principal plane,
 * so EFL = -y_in/u'_out, and it crosses it BFD behind the rear vertex, so
 * BFD = -y_out/u'_out. */
static void ynu_trace(const OsLens *L, ls_real lambda_nm, ls_real y0, ls_real u0,
                      ls_real *y_out, ls_real *u_out) {
    ls_real y = y0, u = u0;
    for (int i = 0; i < L->nsurf; ++i) {
        ls_real n  = os_lens_n_before(L, i, lambda_nm);
        ls_real np = os_lens_n_after(L, i, lambda_nm);
        ls_real R  = L->surf[i].radius_mm;

        ls_real power = (fabs(R) < 1e-12) ? 0.0 : (np - n) / R;
        u = (n * u - y * power) / np;

        if (i + 1 < L->nsurf) y += u * L->surf[i].thickness_mm;
    }
    *y_out = y;
    *u_out = u;
}

void os_lens_paraxial(const OsLens *L, ls_real lambda_nm,
                      ls_real *efl, ls_real *bfd, ls_real *pp_rear) {
    ls_real y, u;
    ynu_trace(L, lambda_nm, 1.0, 0.0, &y, &u);

    /* u == 0 means the lens has no power -- an afocal stack, or a bug. Report
     * infinity rather than dividing by zero, so a caller sees "no focus"
     * instead of a NaN that spreads silently through everything downstream. */
    ls_real f = (fabs(u) < 1e-15) ? HUGE_VAL : -1.0 / u;
    ls_real b = (fabs(u) < 1e-15) ? HUGE_VAL : -y / u;

    if (efl)     *efl = f;
    if (bfd)     *bfd = b;
    /* The rear principal plane is where the extended incoming ray meets the
     * extended outgoing one; it sits BFD - EFL from the rear vertex. */
    if (pp_rear) *pp_rear = b - f;
}

ls_real os_lens_efl_at(const OsLens *L, ls_real lambda_nm) {
    ls_real f;
    os_lens_paraxial(L, lambda_nm, &f, NULL, NULL);
    return f;
}

/* ---- the pupils --------------------------------------------------------
 *
 * A pupil is the IMAGE of the aperture stop: the entrance pupil is the stop
 * seen from in front, through whatever glass precedes it; the exit pupil is
 * the stop seen from behind, through whatever follows it. They matter for
 * different things -- the entrance pupil defines the f-number, and the exit
 * pupil subtends the blur cone at the film and so sets the circle of
 * confusion.
 *
 * FINDING THE IMAGE OF A POINT NEEDS TWO RAYS.
 *   The first version of this traced ONE ray leaving the stop parallel to the
 *   axis and reported where it crossed. That is not the image of the stop; it
 *   is the FOCAL POINT of the group, because a ray parallel to the axis is by
 *   definition the one that defines the focus. It gave a pupil roughly a focal
 *   length from where the pupil actually is, and therefore a defocus blur off
 *   by a factor of six -- while still producing a plausible number that scaled
 *   correctly with aperture.
 *
 *   The image of a point is where rays LEAVING THAT POINT reconverge. So trace
 *   two, from the same point at different slopes, and intersect them. */
typedef struct { ls_real y, u; } YU;

/* One paraxial ray from (height y0, slope u0) at z0, through surfaces i0..i1
 * in the direction of travel, ending at the plane z_end. `forward` selects
 * whether i runs up (toward the sensor) or down (toward the object); the
 * indices swap with the direction. */
static YU ynu_range(const OsLens *L, ls_real lambda_nm, bool forward,
                    int i0, int i1, ls_real y0, ls_real u0,
                    ls_real z0, ls_real z_end) {
    ls_real y = y0, u = u0, z = z0;
    int step = forward ? 1 : -1;
    for (int i = i0; forward ? (i <= i1) : (i >= i1); i += step) {
        ls_real zi = os_lens_vertex_z(L, i);
        y += u * (zi - z);
        z = zi;
        ls_real n  = forward ? os_lens_n_before(L, i, lambda_nm)
                             : os_lens_n_after(L, i, lambda_nm);
        ls_real np = forward ? os_lens_n_after(L, i, lambda_nm)
                             : os_lens_n_before(L, i, lambda_nm);
        ls_real R  = L->surf[i].radius_mm;
        /* R IS NOT NEGATED ON THE WAY BACK, and it used to be.
         *
         * Which convention `u` is in decides this, and the transfer three lines
         * up settles it: `zi - z` is a SIGNED z displacement, negative when
         * walking backward, so `u` is dy/dz in the lens's own fixed z frame at
         * both ends of the loop. It is not a slope along the direction of
         * travel, and the system is never mirrored.
         *
         * In that frame the paraxial refraction relation
         *
         *     n_hi * u_hi = n_lo * u_lo - y * (n_hi - n_lo) / R
         *
         * is one equation relating the two sides of the surface, and it does
         * not care which way the ray is going -- only which side it is coming
         * from, which is what the n/np swap above already expresses. Negating R
         * as WELL applied the mirrored convention on top of the unmirrored one
         * and flipped the sign of the curvature term. On a stop 10 mm inside an
         * n = 1.5 medium imaged back through an R = +100 surface it put the
         * image at z = 6.4516 with magnification 0.9677, where the closed form
         * for a single refracting surface says 6.8966 and 1.0345.
         *
         * The other backward trace in this file -- the front focal distance in
         * os_lens_build -- is the mirrored one, and correctly negates R,
         * because ITS transfer adds a POSITIVE thickness. Two backward walks,
         * two conventions; the tested one was the other one.
         *
         * Nothing shipped changed when this was fixed: every prescription here
         * puts the stop on surface 0, so entrance_pupil short-circuits and this
         * branch is only reached by a design with the stop behind the front
         * element. It is reached the moment anyone adds a double Gauss. */
        ls_real power = (fabs(R) < 1e-12) ? 0.0 : (np - n) / R;
        u = (n * u - y * power) / np;
    }
    y += u * (z_end - z);
    YU r = { y, u };
    return r;
}

/* Image the stop plane through surfaces i0..i1, reporting the magnification
 * and where the image sits relative to z_ref. */
static void image_stop(const OsLens *L, ls_real lambda_nm, bool forward,
                       int i0, int i1, ls_real z_stop, ls_real z_ref,
                       ls_real *mag, ls_real *z_img) {
    /* Two rays from the same stop-edge point, at different slopes. */
    const ls_real U = 0.02;
    YU a = ynu_range(L, lambda_nm, forward, i0, i1, 1.0,  0.0, z_stop, z_ref);
    YU b = ynu_range(L, lambda_nm, forward, i0, i1, 1.0,  U,   z_stop, z_ref);

    ls_real du = a.u - b.u;
    if (fabs(du) < 1e-15) {
        /* The two rays stayed parallel: the group is afocal for this plane, so
         * the image is at infinity. Report the stop unchanged rather than an
         * infinity that would propagate into every blur calculation. */
        *mag = 1.0;
        *z_img = 0.0;
        return;
    }
    ls_real t = (b.y - a.y) / du;
    *z_img = t;                     /* relative to z_ref */
    *mag   = a.y + a.u * t;         /* the object height was 1 */
}

static void entrance_pupil(OsLens *L, ls_real lambda_nm) {
    int s = L->stop_index;
    ls_real z_stop = os_lens_vertex_z(L, s);

    if (s <= 0) {
        /* Nothing in front of the stop: it IS the entrance pupil. */
        L->ep_z_mm = z_stop;
        L->ep_mag  = 1.0;
        L->ep_semi_ap_mm = L->stop_semi_ap_mm;
        return;
    }
    ls_real mag, z;
    image_stop(L, lambda_nm, false, s - 1, 0, z_stop, 0.0, &mag, &z);
    L->ep_mag = mag;
    L->ep_z_mm = z;                              /* from the front vertex */
    L->ep_semi_ap_mm = L->stop_semi_ap_mm * fabs(mag);
}

static void exit_pupil(OsLens *L, ls_real lambda_nm) {
    int s = L->stop_index;
    ls_real z_stop = os_lens_vertex_z(L, s);
    ls_real z_rear = os_lens_vertex_z(L, L->nsurf - 1);

    if (s >= L->nsurf - 1) {
        L->xp_mag = 1.0;
        L->xp_semi_ap_mm = L->stop_semi_ap_mm;
        L->xp_z_mm = z_stop - z_rear;
        return;
    }
    ls_real mag, z;
    image_stop(L, lambda_nm, true, s + 1, L->nsurf - 1, z_stop, z_rear, &mag, &z);
    L->xp_mag = mag;
    L->xp_z_mm = z;                              /* from the REAR vertex */
    L->xp_semi_ap_mm = L->stop_semi_ap_mm * fabs(mag);
}

ls_real os_lens_coc_mm(const OsLens *L, ls_real object_distance_m) {
    ls_real s_prime;
    if (isinf(object_distance_m)) {
        s_prime = L->efl_mm;
    } else {
        ls_real s_from_pp = object_distance_m * 1000.0 - (L->ffd_mm + L->efl_mm);
        if (s_from_pp <= L->efl_mm) return HUGE_VAL;
        s_prime = 1.0 / (1.0 / L->efl_mm - 1.0 / s_from_pp);
    }
    ls_real z_img = L->pp_rear_mm + s_prime;         /* from the rear vertex */
    ls_real defocus = fabs(L->film_z_mm - z_img);

    /* Similar triangles from the exit pupil: the cone it subtends has narrowed
     * to a point at z_img, so at the film it is this wide. */
    ls_real lever = z_img - L->xp_z_mm;
    if (!(fabs(lever) > 1e-9)) return HUGE_VAL;
    return 2.0 * L->xp_semi_ap_mm * defocus / fabs(lever);
}

bool os_lens_set_fnumber(OsLens *L, ls_real fno) {
    if (!(fno > 0.0)) return false;

    /* Wanted entrance-pupil DIAMETER is efl/N, so its radius is efl/(2N). The
     * stop that produces it is smaller by the pupil magnification. */
    ls_real want_ep_semi = L->efl_mm / (2.0 * fno);

    /* The magnification does not depend on the stop size, so it can be found
     * once with a provisional stop and then inverted. */
    L->stop_semi_ap_mm = want_ep_semi;   /* provisional, to seed the trace */
    entrance_pupil(L, OS_LINE_D);
    ls_real m = fabs(L->ep_mag) > 1e-12 ? fabs(L->ep_mag) : 1.0;

    ls_real stop = want_ep_semi / m;

    /* The iris cannot open wider than the mechanical hole it sits in: a 50 mm
     * design whose front element is 20 mm across cannot be an f/1.4 lens no
     * matter what the caller asks for. Clamping is the right behaviour -- a UI
     * dragging the aperture should stop at the limit rather than error -- but
     * the clamp MUST be reflected back in f_number. Storing the requested
     * value here instead would make the lens report f/1.4 while passing f/5
     * worth of light, and every exposure computed from it would be wrong by
     * that ratio with nothing to show for it. */
    ls_real limit = L->surf[L->stop_index].semi_ap_mm;
    bool clamped = stop > limit;
    if (clamped) stop = limit;

    L->stop_semi_ap_mm = stop;
    entrance_pupil(L, OS_LINE_D);        /* recompute with the final stop */
    exit_pupil(L, OS_LINE_D);

    L->f_number = clamped ? L->efl_mm / (2.0 * L->ep_semi_ap_mm) : fno;
    return true;
}

bool os_lens_design_focal_range(OsPrescriptionId id,
                                ls_real *min_mm, ls_real *max_mm) {
    OsPrescription p;
    if (!os_prescription(id, 0.0, &p)) return false;
    if (!(p.focal_min_mm > 0.0) || !(p.focal_max_mm > p.focal_min_mm)) return false;
    if (min_mm) *min_mm = p.focal_min_mm;
    if (max_mm) *max_mm = p.focal_max_mm;
    return true;
}

bool os_lens_focal_range_mm(const OsLens *L, ls_real *min_mm, ls_real *max_mm) {
    if (!(L->focal_min_mm > 0.0) || !(L->focal_max_mm > L->focal_min_mm))
        return false;
    if (min_mm) *min_mm = L->focal_min_mm;
    if (max_mm) *max_mm = L->focal_max_mm;
    return true;
}

ls_real os_lens_min_fnumber(const OsLens *L) {
    /* The same arithmetic set_fnumber's clamp performs, run forward: the stop
     * can grow to the bore, the entrance pupil is that magnified by the glass
     * in front of it, and the f-number is the focal length over its diameter.
     * ep_mag does not depend on the stop's SIZE -- image_stop traces a unit
     * height -- so this is a property of the design, not of its current
     * setting. */
    ls_real limit = L->surf[L->stop_index].semi_ap_mm;
    ls_real m = fabs(L->ep_mag) > 1e-12 ? fabs(L->ep_mag) : 1.0;
    ls_real widest = limit * m;
    return widest > 0.0 ? L->efl_mm / (2.0 * widest) : HUGE_VAL;
}

/* ---- focus --------------------------------------------------------------
 *
 * Newton on the film position would be overkill here: for an object at
 * distance s the paraxial image distance from the REAR principal plane is
 * given by the thick-lens conjugate equation, and the film sits that far
 * behind the rear principal plane.
 *
 *     1/s' = 1/f + 1/s        (s measured positive toward the object)
 *
 * At infinity s' = f, and the film lands at BFD behind the rear vertex, which
 * is the definition of BFD -- so the two paths agree at the limit, and that
 * agreement is asserted in the tests. */
bool os_lens_focus(OsLens *L, ls_real distance_m) {
    if (!(distance_m > 0.0)) return false;

    if (isinf(distance_m)) {
        L->film_z_mm = L->bfd_mm;
        L->focus_distance_m = HUGE_VAL;
        return true;
    }

    ls_real s_mm = distance_m * 1000.0;
    /* Object distance is measured from the FRONT principal plane, so subtract
     * the front principal plane offset from the front vertex. Working in the
     * thick-lens form keeps this correct for a long telephoto, where the
     * principal planes sit well outside the glass. */
    ls_real s_from_pp = s_mm - (L->ffd_mm + L->efl_mm);
    if (s_from_pp <= L->efl_mm) return false;   /* inside the front focal point */

    ls_real s_prime = 1.0 / (1.0 / L->efl_mm - 1.0 / s_from_pp);
    L->film_z_mm = L->pp_rear_mm + s_prime;
    L->focus_distance_m = distance_m;
    return true;
}

ls_real os_lens_transmittance(const OsLens *L, ls_real lambda_nm) {
    ls_real t = 1.0;
    for (int i = 0; i < L->nsurf; ++i) {
        ls_real n  = os_lens_n_before(L, i, lambda_nm);
        ls_real np = os_lens_n_after(L, i, lambda_nm);
        if (fabs(np - n) < 1e-12) continue;      /* not an optical interface */
        /* At normal incidence, which is what "on axis" means. The vendored
         * Fresnel function finally gets a caller. */
        t *= 1.0 - ls_fresnel_dielectric(1.0, n / np);
    }
    return t;
}

ls_real os_lens_half_fov_deg(const OsLens *L, ls_real sensor_diagonal_mm) {
    if (!(L->efl_mm > 0.0)) return 0.0;
    return atan2(sensor_diagonal_mm * 0.5, L->efl_mm) * 180.0 / LS_PI;
}

/* ---- build -------------------------------------------------------------- */

/* How far a surface reaches from its own vertex at height h, signed along +z.
 *
 * Positive R curves toward the image and the cap bulges that way; negative R
 * curves the other way. A plano reaches nowhere. Clamped at |R| because a
 * semi-aperture wider than the radius describes a hemisphere, and the sag there
 * is exactly R rather than a NaN. */
static ls_real surface_sag_mm(ls_real radius_mm, ls_real h) {
    if (fabs(radius_mm) < 1e-12) return 0.0;
    ls_real r2 = radius_mm * radius_mm, h2 = h * h;
    ls_real root = h2 >= r2 ? 0.0 : sqrt(r2 - h2);
    return radius_mm > 0.0 ? radius_mm - root : radius_mm + root;
}

/* The FRONT focal distance, by the y-nu trace run from the other end.
 *
 * Extracted from os_lens_build because it is no longer computed once: a
 * parametric design is measured at several separations before one is chosen,
 * and every measurement needs the whole first-order set. Note the MIRRORED
 * convention -- a positive thickness added while walking backward, and the
 * radius negated to match -- which is a different frame from ynu_range's and
 * is why that function does NOT negate its radius. See the note there. */
static void first_order(OsLens *L) {
    os_lens_paraxial(L, OS_LINE_D, &L->efl_mm, &L->bfd_mm, &L->pp_rear_mm);

    ls_real y = 1.0, u = 0.0;
    for (int i = L->nsurf - 1; i >= 0; --i) {
        ls_real n  = os_lens_n_after(L, i, OS_LINE_D);
        ls_real np = os_lens_n_before(L, i, OS_LINE_D);
        ls_real R  = L->surf[i].radius_mm;
        if (i + 1 < L->nsurf) y += u * L->surf[i].thickness_mm;
        ls_real power = (fabs(R) < 1e-12) ? 0.0 : (np - n) / (-R);
        u = (n * u - y * power) / np;
    }
    /* NEGATED, because this ray travels in -z: a crossing 100 mm ahead of the
     * front vertex sits at z = -100 in the lens's own coordinates. Without the
     * flip a thin lens reports its front focal point 100 mm BEHIND its vertex,
     * which puts the front principal plane 200 mm out and makes every focus
     * distance wrong -- consistently, and in a way that still focuses on
     * something. */
    L->ffd_mm = (fabs(u) < 1e-15) ? -HUGE_VAL : y / u;

    L->total_track_mm = os_lens_vertex_z(L, L->nsurf - 1);
}

/* Copy a prescription into the lens and resolve its glasses. Returns the
 * offending surface index, or -1 on success. */
static int load_prescription(OsLens *L, const OsPrescription *p) {
    L->nsurf = p->nsurf;
    L->stop_index = p->stop_index >= 0 ? p->stop_index : 0;
    L->image_circle_mm = p->image_circle_mm;
    L->focal_min_mm = p->focal_min_mm;
    L->focal_max_mm = p->focal_max_mm;
    for (int i = 0; i < p->nsurf; ++i) {
        L->surf[i] = p->surf[i];
        if (!os_glassref_resolve(p->surf[i].glass, &L->glass[i])) return i;
    }
    return -1;
}

bool os_lens_build(OsLens *L, OsPrescriptionId id, ls_real efl_mm,
                   ls_real fno, char *why, size_t nwhy) {
    OsPrescription p;
    if (!os_prescription(id, 0.0, &p)) {
        snprintf(why, nwhy, "no such prescription (%d)", (int)id);
        return false;
    }

    /* A prescription with more surfaces than the lens can hold would run off
     * the end of two fixed arrays without a word. Nothing shipped does, but a
     * design is data and this is the only place it is trusted. */
    if (p.nsurf < 1 || p.nsurf > OS_MAX_SURF) {
        snprintf(why, nwhy, "%s: %d surfaces, and the limit is %d",
                 p.name, p.nsurf, OS_MAX_SURF);
        return false;
    }

    memset(L, 0, sizeof *L);
    snprintf(L->name, sizeof L->name, "%s", p.name);

    /* ---- a FAMILY is solved, not scaled ----
     *
     * A parametric design already has the focal length in its geometry: the
     * separation between its groups IS the zoom setting. So instead of
     * measuring the lens and multiplying every length by a ratio, find the
     * separation whose measured focal length is the one asked for.
     *
     * Solved by bisection against the paraxial trace rather than from the
     * thin-lens identity, because the groups are 6.5 mm of glass each and the
     * identity is out by 14 % at the long end. Focal length falls monotonically
     * as the groups separate, which is what makes a bisection legitimate. */
    if (p.parametric) {
        ls_real want = efl_mm > 0.0
                     ? efl_mm
                     : sqrt(p.focal_min_mm * p.focal_max_mm);   /* mid-range */
        if (want < p.focal_min_mm * (1.0 - 1e-9) ||
            want > p.focal_max_mm * (1.0 + 1e-9)) {
            /* Say WHICH end, because the two ends fail for different
             * reasons and the reader deserves the right one. */
            snprintf(why, nwhy,
                     "%s: %.4g mm is outside what this design's groups reach "
                     "(%.4g to %.4g mm) -- %s",
                     p.name, want, p.focal_min_mm, p.focal_max_mm,
                     want > p.focal_max_mm
                        ? "closer than its shortest separation the groups collide"
                        : "past the wide end its front element stops covering "
                          "the frame");
            return false;
        }

        ls_real lo = p.param_min, hi = p.param_max;
        for (int it = 0; it < 60; ++it) {
            ls_real mid = 0.5 * (lo + hi);
            OsPrescription q;
            if (!os_prescription(id, mid, &q)) break;
            int bad = load_prescription(L, &q);
            if (bad >= 0) {
                snprintf(why, nwhy, "%s: surface %d names a glass that would "
                                    "not resolve", q.name, bad);
                return false;
            }
            first_order(L);
            if (!isfinite(L->efl_mm) || L->efl_mm <= 0.0) { lo = mid; continue; }
            /* Longer than wanted means the groups need to move apart. */
            if (L->efl_mm > want) lo = mid; else hi = mid;
        }
        /* Rebuild at the settled separation so the lens carries it, and tell
         * the gate below what this setting was supposed to deliver. */
        OsPrescription q;
        if (!os_prescription(id, 0.5 * (lo + hi), &q)) {
            snprintf(why, nwhy, "%s: the separation solve did not converge",
                     p.name);
            return false;
        }
        q.design_efl_mm = want;
        p = q;
    }

    int bad = load_prescription(L, &p);
    if (bad >= 0) {
        snprintf(why, nwhy, "%s: surface %d names a glass that would not "
                            "resolve", p.name, bad);
        return false;
    }

    /* First-order properties, at the d line, before anything can use them. */
    first_order(L);
    if (!isfinite(L->efl_mm) || L->efl_mm <= 0.0) {
        snprintf(why, nwhy, "%s: paraxial focal length is not a positive "
                            "finite number (%.4g)", p.name, L->efl_mm);
        return false;
    }

    /* THE transcription gate. A prescription whose paraxial focal length does
     * not match its stated design value has a typo in it, and the resulting
     * lens would render a completely convincing image of the wrong field of
     * view -- which is exactly the kind of error nobody finds by looking. */
    ls_real want = p.design_efl_mm;
    if (fabs(L->efl_mm - want) > 0.01 * want) {
        snprintf(why, nwhy,
                 "%s: paraxial EFL %.4f mm but the design says %.4f mm "
                 "(%.2f%% off) -- check the table's radii and signs",
                 p.name, L->efl_mm, want, 100.0 * (L->efl_mm - want) / want);
        return false;
    }

    /* THE OTHER transcription gate: the surfaces must not pass through each
     * other.
     *
     * A prescription is a list of vertex positions, but a surface is not AT its
     * vertex -- a curved one reaches sag(h) = R - sqrt(R^2 - h^2) away from it,
     * and at a wide clear aperture that is millimetres. If surface i reaches
     * past surface i+1 anywhere inside the aperture they share, the two are
     * interpenetrating, and the SEQUENTIAL tracer cannot cope: it visits
     * surfaces in prescription order rather than in hit order, so a ray reaches
     * the second one having already flown through where the first was and then
     * finds it behind itself. os_surface_hit wants t > 0, gets neither root
     * positive, and reports the ray vignetted.
     *
     * The failure mode is what makes this worth a gate rather than a comment: a
     * lens like that does not error, it renders BLACK, and a black render is
     * indistinguishable from a lighting mistake. The ideal lens shipped that
     * way -- its two surfaces shared a vertex while the first bulged 13.4 mm
     * past it -- and nothing here noticed, because nothing here rendered
     * through it.
     *
     * Checked at the smaller of the two clear semi-apertures, which is the
     * widest height at which both surfaces exist. The real designs pass with
     * millimetres to spare: 3.03 mm of edge clearance on the singlet, 3.78 and
     * 2.61 on the achromat. Scale-invariant, since os_lens_build scales every
     * length by the same k. */
    for (int i = 0; i + 1 < L->nsurf; ++i) {
        ls_real h = ls_min(L->surf[i].semi_ap_mm, L->surf[i + 1].semi_ap_mm);
        ls_real z0 = os_lens_vertex_z(L, i)     + surface_sag_mm(L->surf[i].radius_mm, h);
        ls_real z1 = os_lens_vertex_z(L, i + 1) + surface_sag_mm(L->surf[i + 1].radius_mm, h);
        if (z0 > z1) {
            snprintf(why, nwhy,
                     "%s: surfaces %d and %d interpenetrate -- at the %.3f mm "
                     "clear semi-aperture the first reaches z = %.3f mm and the "
                     "second only %.3f mm, so a sequential trace cannot reach "
                     "them in order and every ray would come back vignetted",
                     p.name, i, i + 1, h, z0, z1);
            return false;
        }
    }

    /* Scale to the REQUESTED focal length, using the focal length actually
     * measured rather than the design's nominal one.
     *
     * Those two differ: the design values come from thin-lens equations, and a
     * real doublet with 4 mm and 2.5 mm elements comes out ~0.4 % shorter. If
     * k were computed from the nominal value, asking for an 85 mm lens would
     * hand back an 84.7 mm one -- small, permanent, and invisible in every
     * image. Paraxial focal length is exactly proportional to k, so measuring
     * once and dividing is exact and needs no iteration. */
    /* A parametric design is NOT scaled. Its separation already delivers the
     * focal length asked for -- that was the solve above -- and scaling it
     * afterwards would multiply the answer by itself. */
    L->scale = 1.0;
    if (efl_mm > 0.0 && !p.parametric) {
        ls_real k = efl_mm / L->efl_mm;
        for (int i = 0; i < L->nsurf; ++i) {
            L->surf[i].radius_mm    *= k;
            L->surf[i].thickness_mm *= k;
            L->surf[i].semi_ap_mm   *= k;
        }
        L->scale = k;
        L->image_circle_mm *= k;
        L->total_track_mm = os_lens_vertex_z(L, L->nsurf - 1);
        os_lens_paraxial(L, OS_LINE_D, &L->efl_mm, &L->bfd_mm, &L->pp_rear_mm);
        L->ffd_mm *= k;
    }

    /* Nine straight blades: the common photographic default, and the one that
     * makes the aperture's effect on bokeh and on starbursts visible rather
     * than academic. An odd blade count gives 2N spikes instead of N, which is
     * why most lenses have an odd one. Changing this changes the SHAPE of the
     * blur and the spike count; os_iris_circumradius keeps it from changing the
     * exposure by so much as a photon. */
    L->blades          = 9;
    L->blade_rot_rad   = 0.0;
    L->blade_curvature = 0.0;

    if (!os_lens_set_fnumber(L, fno > 0.0 ? fno : p.design_fno)) {
        snprintf(why, nwhy, "%s: f/%.3g is not a usable aperture", p.name, fno);
        return false;
    }
    os_lens_focus(L, HUGE_VAL);

    if (nwhy > 0) why[0] = '\0';
    return true;
}

/* ---- real sequential ray tracing ---------------------------------------- */

bool os_surface_hit(ls_real radius_mm, ls_real z_vertex,
                    const OsLensRay *r, ls_real *t_out, vec3 *n_out) {
    if (fabs(radius_mm) < 1e-12) {
        /* Plano: a plane at z = z_vertex. A ray travelling perpendicular to
         * the axis never meets it, and dividing by d.z would give an infinity
         * that survives as a plausible-looking coordinate. */
        if (fabs(r->d.z) < 1e-12) return false;
        ls_real t = (z_vertex - r->o.z) / r->d.z;
        if (t <= 0.0) return false;
        *t_out = t;
        *n_out = v3(0.0, 0.0, 1.0);
        return true;
    }

    /* Sphere centred on the axis, one radius beyond the vertex. */
    ls_real cz = z_vertex + radius_mm;
    vec3 m = v3(r->o.x, r->o.y, r->o.z - cz);

    ls_real b = v3dot(m, r->d);
    ls_real c = v3dot(m, m) - radius_mm * radius_mm;
    ls_real disc = b * b - c;
    if (disc < 0.0) return false;
    ls_real sq = sqrt(disc);
    ls_real t0 = -b - sq, t1 = -b + sq;

    /* WHICH root is the surface depends on the direction of travel AND the
     * sign of the curvature. Taking -b - sqrt(disc) unconditionally picks the
     * far side of the sphere for a negative radius: the lens still traces, it
     * just uses the wrong cap, and the image it forms looks like a real image
     * with the wrong aberrations. The exclusive-or is the whole rule. */
    bool use_closer = (r->d.z > 0.0) != (radius_mm < 0.0);
    ls_real t = use_closer ? ls_min(t0, t1) : ls_max(t0, t1);
    if (t <= 0.0) {
        t = use_closer ? ls_max(t0, t1) : ls_min(t0, t1);
        if (t <= 0.0) return false;
    }

    vec3 p = v3add(r->o, v3scale(r->d, t));
    *t_out = t;
    /* Outward from the centre, unflipped -- os_refract orients it. */
    *n_out = v3norm(v3(p.x, p.y, p.z - cz));
    return true;
}

bool os_refract(vec3 d, vec3 n, ls_real eta, vec3 *out) {
    /* Orient the normal against the incoming ray so cos_i is positive; this is
     * the one place a normal may be flipped, and it is flipped for the local
     * arithmetic only, never stored. */
    ls_real cos_i = -v3dot(d, n);
    if (cos_i < 0.0) { n = v3scale(n, -1.0); cos_i = -cos_i; }

    ls_real k = 1.0 - eta * eta * (1.0 - cos_i * cos_i);
    if (k < 0.0) return false;                 /* total internal reflection */

    *out = v3norm(v3add(v3scale(d, eta),
                        v3scale(n, eta * cos_i - sqrt(k))));
    return true;
}

ls_real os_iris_circumradius(ls_real a, int blades, ls_real curvature) {
    if (blades < 3) return a;

    /* The blade boundary at angle phi from an edge's midpoint normal is
     *
     *     r(phi) = rho [ (1-c) k / cos(phi) + c ],   k = cos(pi/N)
     *
     * blending the straight chord toward the circumscribed circle. Its area is
     * the integral of r^2/2 over the full turn, which has a closed form: with
     * m = pi/N and using  int sec^2 = tan,  int sec = ln|sec + tan|,
     *
     *     A(rho=1) = N [ (1-c)^2 k^2 tan m + 2c(1-c) k ln(sec m + tan m) + c^2 m ]
     *
     * and rho then follows from A(rho) = rho^2 A(1) = pi a^2.
     *
     * The obvious shortcut -- blend rho linearly between the polygon value and
     * the circle value -- is WRONG, and wrong in a way that hides: area goes
     * as rho^2, so a linear blend of rho is not a linear blend of area. It is
     * exact at c = 0 and c = 1 and worst in between, which is precisely where
     * nobody thinks to check. It overshot by 9 % on a three-blade iris at
     * half curvature: a third of a stop of exposure error produced by a
     * control that is supposed to change only the shape of the blur. */
    ls_real c = ls_clamp(curvature, 0.0, 1.0);
    ls_real m = LS_PI / (ls_real)blades;
    ls_real k = cos(m);
    ls_real tan_m = tan(m);
    ls_real sec_m = 1.0 / k;

    ls_real a1 = (ls_real)blades * ((1.0 - c) * (1.0 - c) * k * k * tan_m
                                  + 2.0 * c * (1.0 - c) * k * log(sec_m + tan_m)
                                  + c * c * m);
    if (a1 <= 0.0) return a;
    return a * sqrt(LS_PI / a1);
}

bool os_lens_aperture_contains(const OsLens *L, ls_real x_mm, ls_real y_mm) {
    ls_real r = sqrt(x_mm * x_mm + y_mm * y_mm);
    if (L->blades < 3) return r <= L->stop_semi_ap_mm;

    ls_real rho = os_iris_circumradius(L->stop_semi_ap_mm, L->blades,
                                       L->blade_curvature);
    ls_real th = LS_TWO_PI / (ls_real)L->blades;

    /* Angle to the nearest blade's midpoint, folded into one sector. */
    ls_real phi = atan2(y_mm, x_mm) - L->blade_rot_rad;
    phi = fmod(phi, th);
    if (phi < 0.0) phi += th;
    phi -= th * 0.5;

    /* A straight blade edge is the chord at distance rho cos(pi/N) from the
     * centre, so its radius at angle phi is rho cos(pi/N)/cos(phi). Blending
     * that toward rho gives the rounded blades a real iris has. */
    ls_real straight = rho * cos(LS_PI / (ls_real)L->blades) / cos(phi);
    return r <= ls_lerp(L->blade_curvature, straight, rho);
}

/* The clear radius that actually clips at surface i: the iris at the stop,
 * the mechanical bore everywhere else. */
static ls_real clip_radius(const OsLens *L, int i) {
    if (i == L->stop_index) return L->stop_semi_ap_mm;
    return L->surf[i].semi_ap_mm;
}

bool os_lens_trace(const OsLens *L, ls_real lambda_nm, OsLensRay *r,
                   ls_real *transmittance) {
    for (int i = 0; i < L->nsurf; ++i) {
        ls_real zv = os_lens_vertex_z(L, i);
        ls_real t;
        vec3 nrm;
        if (!os_surface_hit(L->surf[i].radius_mm, zv, r, &t, &nrm)) return false;

        vec3 p = v3add(r->o, v3scale(r->d, t));

        /* THE clip. This, and only this, is where vignetting comes from. */
        if (i == L->stop_index) {
            if (!os_lens_aperture_contains(L, p.x, p.y)) return false;
        } else {
            ls_real rad = clip_radius(L, i);
            if (p.x * p.x + p.y * p.y > rad * rad) return false;
        }

        ls_real n  = os_lens_n_before(L, i, lambda_nm);
        ls_real np = os_lens_n_after(L, i, lambda_nm);

        r->o = p;
        if (fabs(np - n) > 1e-12) {
            vec3 d2;
            if (!os_refract(r->d, nrm, n / np, &d2)) return false;  /* TIR */
            r->d = d2;
            if (transmittance) {
                ls_real cos_i = fabs(v3dot(r->d, nrm));
                *transmittance *= 1.0 - ls_fresnel_dielectric(cos_i, n / np);
            }
        }
    }
    return true;
}

bool os_lens_trace_path(const OsLens *L, ls_real lambda_nm, OsLensRay start,
                        ls_real to_z_mm, OsRayPath *path) {
    path->n = 0;
    path->blocked_at = -1;
    path->p[path->n++] = start.o;

    OsLensRay r = start;
    for (int i = 0; i < L->nsurf; ++i) {
        ls_real zv = os_lens_vertex_z(L, i);
        ls_real t;
        vec3 nrm;
        if (!os_surface_hit(L->surf[i].radius_mm, zv, &r, &t, &nrm)) {
            path->blocked_at = i;
            return false;
        }
        vec3 p = v3add(r.o, v3scale(r.d, t));
        path->p[path->n++] = p;

        bool blocked;
        if (i == L->stop_index) {
            blocked = !os_lens_aperture_contains(L, p.x, p.y);
        } else {
            ls_real rad = clip_radius(L, i);
            blocked = (p.x * p.x + p.y * p.y > rad * rad);
        }
        if (blocked) { path->blocked_at = i; return false; }

        ls_real n  = os_lens_n_before(L, i, lambda_nm);
        ls_real np = os_lens_n_after(L, i, lambda_nm);
        r.o = p;
        if (fabs(np - n) > 1e-12) {
            vec3 d2;
            if (!os_refract(r.d, nrm, n / np, &d2)) { path->blocked_at = i; return false; }
            r.d = d2;
        }
    }

    /* Carry on to the requested plane, normally the film. */
    if (fabs(r.d.z) > 1e-12) {
        ls_real t = (to_z_mm - r.o.z) / r.d.z;
        if (t > 0.0 && path->n < OS_PATH_MAX)
            path->p[path->n++] = v3add(r.o, v3scale(r.d, t));
    }
    return true;
}

ls_real os_lens_real_focus_z(const OsLens *L, ls_real lambda_nm,
                             ls_real distance_m, ls_real pupil_fraction) {
    /* Launch a ray from the axial object point toward the entrance pupil at
     * the given fraction of its radius, trace it for real, and see where it
     * actually crosses the axis. As pupil_fraction -> 0 this must converge to
     * the paraxial back focal distance; that it does NOT converge at larger
     * fractions is spherical aberration, measured rather than modelled. */
    ls_real h = pupil_fraction * L->ep_semi_ap_mm;
    if (h <= 0.0) h = 1e-6;

    vec3 o, target;
    if (isinf(distance_m)) {
        o = v3(h, 0.0, -10.0);              /* parallel to the axis */
        target = v3(h, 0.0, 0.0);
    } else {
        ls_real z0 = -distance_m * 1000.0;
        o = v3(0.0, 0.0, z0);
        target = v3(h, 0.0, L->ep_z_mm);
    }

    OsLensRay r = { o, v3norm(v3sub(target, o)) };
    if (!os_lens_trace(L, lambda_nm, &r, NULL)) return HUGE_VAL;

    /* Where this ray crosses the axis, measured from the REAR vertex so it is
     * directly comparable with bfd_mm and film_z_mm. */
    if (fabs(r.d.x) < 1e-15) return HUGE_VAL;
    ls_real t = -r.o.x / r.d.x;
    ls_real z = r.o.z + t * r.d.z;
    return z - os_lens_vertex_z(L, L->nsurf - 1);
}

ls_real os_lens_film_z(const OsLens *L) {
    return os_lens_vertex_z(L, L->nsurf - 1) + L->film_z_mm;
}

bool os_lens_trace_reverse(const OsLens *L, ls_real lambda_nm, OsLensRay *r,
                           ls_real *transmittance) {
    /* Surfaces in reverse, and the indices swap with them: travelling from the
     * film, the medium the ray is IN at surface i is the one that follows i,
     * and the medium it enters is the one that precedes i. Getting that pair
     * the wrong way round produces a lens that still focuses -- with every
     * index inverted, so it focuses in the wrong place and with the wrong
     * aberrations. */
    for (int i = L->nsurf - 1; i >= 0; --i) {
        ls_real zv = os_lens_vertex_z(L, i);
        ls_real t;
        vec3 nrm;
        if (!os_surface_hit(L->surf[i].radius_mm, zv, r, &t, &nrm)) return false;

        vec3 p = v3add(r->o, v3scale(r->d, t));

        if (i == L->stop_index) {
            if (!os_lens_aperture_contains(L, p.x, p.y)) return false;
        } else {
            ls_real rad = L->surf[i].semi_ap_mm;
            if (p.x * p.x + p.y * p.y > rad * rad) return false;
        }

        ls_real n  = os_lens_n_after(L, i, lambda_nm);    /* the medium we are in */
        ls_real np = os_lens_n_before(L, i, lambda_nm);   /* the one we enter     */

        r->o = p;
        if (fabs(np - n) > 1e-12) {
            vec3 d2;
            if (!os_refract(r->d, nrm, n / np, &d2)) return false;
            r->d = d2;
            if (transmittance) {
                ls_real cos_i = fabs(v3dot(r->d, nrm));
                *transmittance *= 1.0 - ls_fresnel_dielectric(cos_i, n / np);
            }
        }
    }
    return true;
}

/* ---- depth of field ----------------------------------------------------- */

/* os_lens_coc_mm is monotone in distance on each side of the focused plane, so
 * a bisection is enough and needs no derivative. Solved rather than taken from
 * the hyperfocal formula so the answer follows the real lens; see lens.h. */
static ls_real solve_coc(const OsLens *L, ls_real limit, ls_real a, ls_real b) {
    for (int i = 0; i < 80; ++i) {
        ls_real m = 0.5 * (a + b);
        ls_real c = os_lens_coc_mm(L, m);
        /* An object inside the front focal point images nowhere, which reads
         * as an infinite blur -- treat it as "too blurred" rather than letting
         * the HUGE_VAL escape into the comparison. */
        if (!isfinite(c) || c > limit) a = m; else b = m;
    }
    return 0.5 * (a + b);
}

bool os_lens_dof(const OsLens *L, ls_real coc_limit_mm,
                 ls_real *near_m, ls_real *far_m) {
    if (!(coc_limit_mm > 0.0)) return false;
    ls_real s = L->focus_distance_m;
    if (!isfinite(s) || !(s > 0.0)) {
        /* Focused at infinity: everything from the hyperfocal distance out. */
        if (near_m) *near_m = os_lens_hyperfocal_m(L, coc_limit_mm);
        if (far_m)  *far_m  = HUGE_VAL;
        return true;
    }

    /* Near side: blur grows without bound as the object approaches the lens,
     * so the bracket is (something very close, focus). */
    if (near_m) *near_m = solve_coc(L, coc_limit_mm, 1e-4, s);

    if (far_m) {
        /* If a very distant object is still inside the limit, the far side is
         * unbounded -- which is what being at or past hyperfocal means. */
        if (os_lens_coc_mm(L, 1e7) <= coc_limit_mm) *far_m = HUGE_VAL;
        else *far_m = solve_coc(L, coc_limit_mm, 1e7, s);
    }
    return true;
}

ls_real os_lens_hyperfocal_m(const OsLens *L, ls_real coc_limit_mm) {
    if (!(coc_limit_mm > 0.0)) return HUGE_VAL;
    /* The hyperfocal distance is where an object at infinity first blurs by
     * exactly the limit. With the lens focused at h, infinity blurs by
     * A*|f - s'(h)|/lever; searching on h directly keeps this in terms of the
     * same os_lens_coc_mm every other answer comes from. */
    OsLens t = *L;
    ls_real lo = 0.05, hi = 1e6;
    for (int i = 0; i < 80; ++i) {
        ls_real m = sqrt(lo * hi);            /* geometric: the range is decades */
        if (!os_lens_focus(&t, m)) { lo = m; continue; }
        if (os_lens_coc_mm(&t, 1e7) > coc_limit_mm) lo = m; else hi = m;
    }
    return sqrt(lo * hi);
}

ls_real os_lens_spot_mm(const OsLens *L, ls_real object_distance_m,
                        ls_real object_height_m, int nrays) {
    if (!(object_distance_m > 0.0) || !isfinite(object_distance_m)) return HUGE_VAL;
    if (nrays < 3) nrays = 3;

    ls_real zf = os_lens_vertex_z(L, L->nsurf - 1) + L->film_z_mm;
    vec3 o = v3(object_height_m * 1000.0, 0.0, -object_distance_m * 1000.0);

    /* A square grid over the entrance pupil, culled to its disc. Two passes:
     * the centroid first, then the spread about it -- the spot's CENTRE moves
     * off axis (that is distortion, and is not blur), so measuring the spread
     * about a fixed point would count it as one. */
    ls_real sx = 0.0, sy = 0.0;
    int n = 0;
    for (int i = 0; i < nrays; ++i) {
        for (int j = 0; j < nrays; ++j) {
            ls_real u = -1.0 + 2.0 * ((ls_real)i + 0.5) / (ls_real)nrays;
            ls_real w = -1.0 + 2.0 * ((ls_real)j + 0.5) / (ls_real)nrays;
            if (u * u + w * w > 1.0) continue;
            vec3 t = v3(u * L->ep_semi_ap_mm, w * L->ep_semi_ap_mm, L->ep_z_mm);
            OsLensRay r = { o, v3norm(v3sub(t, o)) };
            if (!os_lens_trace(L, OS_LINE_D, &r, NULL)) continue;
            if (fabs(r.d.z) < 1e-15) continue;
            ls_real tt = (zf - r.o.z) / r.d.z;
            sx += r.o.x + tt * r.d.x;
            sy += r.o.y + tt * r.d.y;
            n++;
        }
    }
    /* Nothing got through: the subject is outside what the lens covers. */
    if (n < 3) return HUGE_VAL;

    ls_real mx = sx / (ls_real)n, my = sy / (ls_real)n;
    ls_real s2 = 0.0;
    for (int i = 0; i < nrays; ++i) {
        for (int j = 0; j < nrays; ++j) {
            ls_real u = -1.0 + 2.0 * ((ls_real)i + 0.5) / (ls_real)nrays;
            ls_real w = -1.0 + 2.0 * ((ls_real)j + 0.5) / (ls_real)nrays;
            if (u * u + w * w > 1.0) continue;
            vec3 t = v3(u * L->ep_semi_ap_mm, w * L->ep_semi_ap_mm, L->ep_z_mm);
            OsLensRay r = { o, v3norm(v3sub(t, o)) };
            if (!os_lens_trace(L, OS_LINE_D, &r, NULL)) continue;
            if (fabs(r.d.z) < 1e-15) continue;
            ls_real tt = (zf - r.o.z) / r.d.z;
            ls_real X = r.o.x + tt * r.d.x - mx;
            ls_real Y = r.o.y + tt * r.d.y - my;
            s2 += X * X + Y * Y;
        }
    }
    /* Reported as a DIAMETER, so it compares directly with os_lens_coc_mm and
     * with the sharpness criterion the user sets. */
    return 2.0 * sqrt(s2 / (ls_real)n);
}

/* ---- distortion ---------------------------------------------------------
 *
 * The one aberration that does not blur anything. Every other defect here
 * spreads a point into a patch; distortion moves the patch, whole and sharp,
 * to the wrong radius. So it cannot be seen in a spot diagram and it cannot be
 * seen on a scattered field of blobs -- it takes points that OUGHT to be
 * collinear, which is why the GRID stage exists.
 *
 * MEASURED AGAINST THE PARAXIAL IMAGE HEIGHT AT THIS CONJUGATE, which is the
 * textbook definition and the only one that is not quietly wrong. It is easy
 * to compare the real height against f*tan(theta) instead -- that is the
 * INFINITE-conjugate formula, and using it on a lens focused at 2 m reports
 * about +3 % of pincushion on a design that actually has half a per cent of
 * barrel. The reference has to move with the focus.
 *
 * The real height comes from the CHIEF ray -- the one through the centre of
 * the entrance pupil -- because that is the ray that defines where the image
 * of a point sits. Not the centroid of the whole bundle: at a field angle
 * where vignetting has eaten one side of the pupil, the centroid shifts for a
 * reason that is not distortion. */
ls_real os_lens_distortion_pct(const OsLens *L, ls_real image_height_mm) {
    /* On axis there is nothing to displace, and nothing to divide by. */
    if (!(image_height_mm > 0.0)) return 0.0;

    /* The object goes at the distance the lens is focused at, so its paraxial
     * image lands ON the film and the only thing left between the paraxial
     * prediction and the traced ray is distortion. Infinity is traced from far
     * enough away that the conjugate is infinite to double precision, which
     * makes the reference f*tan(theta) again -- correctly, this time, because
     * that is the conjugate the lens is actually set for. */
    ls_real dist_mm = isfinite(L->focus_distance_m)
                    ? L->focus_distance_m * 1000.0 : 1.0e9;
    /* Infinity gets a stand-in distance rather than a special case: at 1e9 mm
     * the conjugate is infinite to well past double precision, so the paraxial
     * reference below collapses to f*tan(theta) on its own. The stand-in is
     * only ever used to define a DIRECTION -- see the launch point below, which
     * is what keeps the arithmetic conditioned. */

    ls_real s_from_pp = dist_mm - (L->ffd_mm + L->efl_mm);
    if (!(s_from_pp > L->efl_mm)) return HUGE_VAL;   /* inside the focal point */

    ls_real s_prime = 1.0 / (1.0 / L->efl_mm - 1.0 / s_from_pp);
    ls_real mag = s_prime / s_from_pp;               /* magnitude; the image
                                                        is inverted and fabs
                                                        below accounts for it */
    if (!(mag > 0.0)) return HUGE_VAL;

    /* The object height whose PARAXIAL image lands exactly where we are
     * asking about. Inverting the magnification rather than sweeping angles
     * is what makes the answer addressable by film position -- the panel asks
     * about the frame corner, which is a position, not an angle. */
    ls_real y_obj = image_height_mm / mag;

    /* The chief ray is the LINE from that object through the pupil centre, and
     * a line can be launched from anywhere along itself. Launch it from close
     * to the glass rather than from the object.
     *
     * That is not tidiness, it is precision. os_surface_hit forms
     * dot(m,m) - R^2 with m the vector from the ray origin to the sphere
     * centre, and for an object a kilometre away that subtracts two numbers
     * near 1e12 to get one near 1e4 -- eight of sixteen digits gone before the
     * trace starts. At the 1e9 mm stand-in an infinite conjugate wants, it is
     * fourteen digits gone, and this function reported +3 % of pincushion on a
     * design with half a per cent of barrel. Ten focal lengths out keeps every
     * intermediate near the size of the lens. */
    vec3 pupil = v3(0.0, 0.0, L->ep_z_mm);
    vec3 dir   = v3norm(v3sub(pupil, v3(y_obj, 0.0, -dist_mm)));
    vec3 o     = v3sub(pupil, v3scale(dir, 10.0 * L->efl_mm));

    OsLensRay r = { o, dir };
    if (!os_lens_trace(L, OS_LINE_D, &r, NULL)) return HUGE_VAL;
    if (fabs(r.d.z) < 1e-15) return HUGE_VAL;

    ls_real zf = os_lens_vertex_z(L, L->nsurf - 1) + L->film_z_mm;
    ls_real t  = (zf - r.o.z) / r.d.z;
    ls_real real_mm = fabs(r.o.x + t * r.d.x);

    /* Positive is PINCUSHION -- the corner lands further out than it should,
     * so a square bows inward. Negative is barrel. Every design shipped here
     * is negative. */
    return 100.0 * (real_mm - image_height_mm) / image_height_mm;
}
