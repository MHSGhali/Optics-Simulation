/* test_trace.c — the real sequential ray tracer.
 *
 * The paraxial suite proved the first-order design. This one proves that rays
 * actually bend correctly through it, and the load-bearing test is the last
 * one: as the ray height goes to zero the real trace must CONVERGE to the
 * paraxial back focal distance, and at larger heights it must diverge
 * monotonically. That convergence is the statement that spherical aberration
 * is being computed rather than invented -- a tracer with a sign error, a
 * wrong root, or a botched Snell would still produce a smooth blur, just not
 * one that agrees with first-order optics in the limit where it must. */
#include "test.h"
#include "tests.h"

#include "opticsim/lens.h"

#include <math.h>
#include <string.h>

/* A deterministic scatter of unit directions, so "random" incidences are
 * reproducible across runs and across thread counts. */
static vec3 dir_sample(int i) {
    ls_real a = 0.7 * (ls_real)i;
    ls_real b = 0.37 * (ls_real)i + 0.11;
    return v3norm(v3(0.4 * sin(a), 0.4 * cos(b), 1.0));
}

void os_test_trace(void) {
    char why[256];

    SECTION("trace: Snell's law, forwards and back");
    {
        vec3 n = v3(0.0, 0.0, -1.0);        /* facing the incoming ray */
        for (int i = 1; i <= 200; ++i) {
            vec3 d = dir_sample(i);
            ls_real eta = 1.0 / 1.5168;      /* air -> N-BK7 */

            vec3 into;
            CHECK(os_refract(d, n, eta, &into));

            /* n1 sin(t1) = n2 sin(t2), stated directly.
             *
             * The sines come from |d x n|, not from sqrt(1 - cos^2). For rays
             * near normal incidence cos is close to 1, so 1 - cos^2 is a
             * difference of two nearly equal numbers and loses most of its
             * significant digits -- that form reports a 3e-13 error on a
             * refraction that is actually exact. The cross product is well
             * conditioned everywhere and measures the same angle. */
            ls_real sin1 = v3len(v3cross(d, n));
            ls_real sin2 = v3len(v3cross(into, n));
            CHECK_NEAR(1.0 * sin1, 1.5168 * sin2, 1e-14);

            /* Entering a medium and leaving it again restores the direction
             * exactly. A sign error in the normal handling survives the first
             * refraction looking plausible and only shows up here. */
            vec3 back;
            CHECK(os_refract(into, n, 1.5168, &back));
            CHECK_NEAR(back.x, d.x, 1e-14);
            CHECK_NEAR(back.y, d.y, 1e-14);
            CHECK_NEAR(back.z, d.z, 1e-14);

            /* Refraction never leaves the plane of incidence. */
            vec3 cr = v3cross(d, into);
            CHECK_NEAR(v3dot(cr, n), 0.0, 1e-14);
        }
    }

    SECTION("trace: total internal reflection starts exactly at the critical angle");
    {
        /* Glass to air: sin(theta_c) = 1/n. Straddling it by 1e-9 in ANGLE
         * pins the boundary rather than merely observing that TIR happens
         * somewhere. */
        const ls_real n = 1.5168;
        ls_real crit = asin(1.0 / n);
        vec3 nrm = v3(0.0, 0.0, -1.0);

        for (ls_real eps = 1e-9; eps < 1e-2; eps *= 10.0) {
            vec3 out;

            ls_real a = crit - eps;          /* just inside: must refract */
            vec3 din = v3(sin(a), 0.0, cos(a));
            CHECK(os_refract(din, nrm, n / 1.0, &out));

            a = crit + eps;                  /* just outside: must not */
            vec3 dout = v3(sin(a), 0.0, cos(a));
            CHECK(!os_refract(dout, nrm, n / 1.0, &out));
        }

        /* And going the other way, into a denser medium, TIR is impossible at
         * any angle whatsoever. */
        for (int i = 1; i <= 100; ++i) {
            vec3 out;
            CHECK(os_refract(dir_sample(i), nrm, 1.0 / n, &out));
        }
    }

    SECTION("trace: spherical surfaces intersect where the algebra says");
    {
        /* A ray along the axis meets a surface exactly at its vertex, for both
         * signs of curvature. This is the case the root-selection rule gets
         * wrong when the exclusive-or is dropped: the far cap of the sphere is
         * also a perfectly good intersection, and using it produces a lens
         * that traces without complaint and images incorrectly. */
        for (int s = 0; s < 2; ++s) {
            ls_real R = s ? -50.0 : 50.0;
            OsLensRay r = { v3(0.0, 0.0, -10.0), v3(0.0, 0.0, 1.0) };
            ls_real t; vec3 n;
            CHECK(os_surface_hit(R, 0.0, &r, &t, &n));
            CHECK_NEAR(t, 10.0, 1e-12);

            /* The normal on axis points straight back along the axis, with the
             * sign following the curvature. */
            CHECK_NEAR(fabs(n.z), 1.0, 1e-12);
        }

        /* Off axis, against the closed form: a sphere of radius R centred at
         * (0,0,R) meets the plane x = h at z = R - sqrt(R^2 - h^2). */
        const ls_real R = 50.0, h = 12.0;
        OsLensRay r = { v3(h, 0.0, -10.0), v3(0.0, 0.0, 1.0) };
        ls_real t; vec3 n;
        CHECK(os_surface_hit(R, 0.0, &r, &t, &n));
        ls_real z = -10.0 + t;
        CHECK_NEAR(z, R - sqrt(R * R - h * h), 1e-12);

        /* The normal is the unit vector from the centre to the hit point. */
        vec3 p = v3(h, 0.0, z);
        vec3 want = v3norm(v3sub(p, v3(0.0, 0.0, R)));
        CHECK_NEAR(n.x, want.x, 1e-12);
        CHECK_NEAR(n.z, want.z, 1e-12);

        /* A plano surface is a plane, and a ray parallel to it misses rather
         * than dividing by zero. */
        OsLensRay flat = { v3(0.0, 0.0, -5.0), v3(0.0, 0.0, 1.0) };
        CHECK(os_surface_hit(0.0, 3.0, &flat, &t, &n));
        CHECK_NEAR(t, 8.0, 1e-12);
        OsLensRay para = { v3(0.0, 0.0, -5.0), v3(1.0, 0.0, 0.0) };
        CHECK(!os_surface_hit(0.0, 3.0, &para, &t, &n));
    }

    SECTION("trace: the iris holds its area whatever its shape");
    {
        /* The f-number is a statement about light-gathering, so changing the
         * blade count must change the SHAPE of the blur and nothing else. If
         * this fails, switching from a circular iris to seven blades darkens
         * the image by 13 % for no physical reason. */
        OsLens L;
        CHECK(os_lens_build(&L, OS_LENS_ACHROMAT_100, 100.0, 8.0, why, sizeof why));

        const ls_real a = L.stop_semi_ap_mm;
        const ls_real want = LS_PI * a * a;

        for (int blades = 3; blades <= 14; ++blades) {
            for (int ci = 0; ci <= 2; ++ci) {
                L.blades = blades;
                L.blade_curvature = 0.5 * (ls_real)ci;
                L.blade_rot_rad = 0.3;

                /* Measured, not asserted from the formula: a dense grid over
                 * the bounding square, counting what the predicate actually
                 * admits. That tests os_lens_aperture_contains and
                 * os_iris_circumradius together, which is the pair that has to
                 * agree. */
                const int N = 601;
                ls_real ext = os_iris_circumradius(a, blades, L.blade_curvature) * 1.05;
                ls_real cell = (2.0 * ext / (ls_real)N) * (2.0 * ext / (ls_real)N);
                ls_real area = 0.0;
                for (int iy = 0; iy < N; ++iy) {
                    ls_real y = -ext + 2.0 * ext * ((ls_real)iy + 0.5) / (ls_real)N;
                    for (int ix = 0; ix < N; ++ix) {
                        ls_real x = -ext + 2.0 * ext * ((ls_real)ix + 0.5) / (ls_real)N;
                        if (os_lens_aperture_contains(&L, x, y)) area += cell;
                    }
                }
                /* 0.5 % is the grid's own resolution on a polygon edge, not
                 * slack in the claim. */
                CHECK_NEAR(area, want, 5e-3);
            }
        }

        /* Fewer than three blades means a circle, and the circumradius is then
         * the stop radius unchanged. */
        CHECK_NEAR(os_iris_circumradius(a, 0, 0.0), a, 1e-12);
        CHECK_NEAR(os_iris_circumradius(a, 2, 0.0), a, 1e-12);
        /* Full curvature is a circle too, whatever the blade count. */
        for (int b = 3; b <= 14; ++b)
            CHECK_NEAR(os_iris_circumradius(a, b, 1.0), a, 1e-12);
        /* Straight blades always need a WIDER circumradius to hold the area. */
        for (int b = 3; b <= 14; ++b)
            CHECK(os_iris_circumradius(a, b, 0.0) > a);
    }

    SECTION("trace: rays through the lens are clipped, never leaked");
    {
        OsLens L;
        CHECK(os_lens_build(&L, OS_LENS_ACHROMAT_100, 100.0, 8.0, why, sizeof why));
        L.blades = 0;                        /* circular, for a clean radius */
        CHECK(os_lens_set_fnumber(&L, 8.0));

        /* A ray aimed at the axis gets through; one aimed well outside the
         * entrance pupil does not. Vignetting is this, and only this. */
        ls_real ep = L.ep_semi_ap_mm;
        for (int i = 0; i <= 20; ++i) {
            ls_real h = ep * (ls_real)i / 10.0;    /* 0 .. 2x the pupil */
            OsLensRay r = { v3(h, 0.0, -1000.0), v3(0.0, 0.0, 1.0) };
            ls_real t = 1.0;
            bool through = os_lens_trace(&L, OS_LINE_D, &r, &t);
            if (h < ep * 0.98)      CHECK(through);
            if (h > ep * 1.02)      CHECK(!through);
            /* Transmittance is a probability: never above one, never below
             * zero, and strictly below one because glass reflects. */
            if (through) { CHECK(t > 0.0); CHECK(t < 1.0); }
        }

        /* Stopping down can only ever admit FEWER rays. */
        int prev = 1 << 30;
        for (double n = 4.0; n <= 32.0; n *= 2.0) {
            CHECK(os_lens_set_fnumber(&L, n));
            int count = 0;
            for (int i = 0; i < 400; ++i) {
                ls_real h = 25.0 * (ls_real)i / 400.0;
                OsLensRay r = { v3(h, 0.0, -1000.0), v3(0.0, 0.0, 1.0) };
                ls_real t = 1.0;
                if (os_lens_trace(&L, OS_LINE_D, &r, &t)) count++;
            }
            CHECK(count <= prev);
            prev = count;
        }
    }

    SECTION("trace: the real focus converges to the paraxial one");
    {
        /* THE test. A marginal ray at height h crosses the axis at a distance
         * that depends on h -- that dependence IS spherical aberration -- and
         * as h goes to zero it must approach the paraxial back focal distance
         * computed by an entirely separate piece of code. */
        OsLens L;
        CHECK(os_lens_build(&L, OS_LENS_ACHROMAT_100, 100.0, 5.0, why, sizeof why));
        L.blades = 0;
        CHECK(os_lens_set_fnumber(&L, 5.0));

        ls_real prev_err = HUGE_VAL;
        for (ls_real frac = 0.4; frac > 1e-4; frac *= 0.5) {
            ls_real z = os_lens_real_focus_z(&L, OS_LINE_D, HUGE_VAL, frac);
            CHECK(isfinite(z));
            ls_real err = fabs(z - L.bfd_mm);
            /* Monotone convergence: halving the aperture height must never
             * make the disagreement worse. */
            CHECK(err <= prev_err + 1e-12);
            prev_err = err;
        }
        NOTE("paraxial BFD %.6f mm; real focus at 1e-4 of the pupil differs by %.2e mm",
             L.bfd_mm, prev_err);
        CHECK(prev_err < 1e-6);

        /* And at a real aperture the disagreement is NOT zero -- an achromat
         * corrects colour, not spherical aberration, so the marginal ray must
         * genuinely focus short of the paraxial one. A tracer that reported no
         * aberration here would be a paraxial trace wearing a disguise. */
        ls_real marginal = os_lens_real_focus_z(&L, OS_LINE_D, HUGE_VAL, 1.0);
        NOTE("longitudinal spherical aberration at full aperture: %.4f mm",
             marginal - L.bfd_mm);
        CHECK(fabs(marginal - L.bfd_mm) > 1e-3);
        /* Undercorrected, as every simple positive lens is: the marginal ray
         * crosses SHORT of the paraxial focus. */
        CHECK(marginal < L.bfd_mm);
    }

    SECTION("trace: colour separates in the direction the glass says");
    {
        /* The singlet must show real, measurable longitudinal colour, and blue
         * must focus short of red -- the same sign the paraxial suite asserted,
         * now confirmed by rays that actually bent. */
        OsLens s;
        CHECK(os_lens_build(&s, OS_LENS_SINGLET_100, 100.0, 8.0, why, sizeof why));
        s.blades = 0;
        CHECK(os_lens_set_fnumber(&s, 8.0));

        ls_real zF = os_lens_real_focus_z(&s, OS_LINE_F, HUGE_VAL, 0.01);
        ls_real zd = os_lens_real_focus_z(&s, OS_LINE_D, HUGE_VAL, 0.01);
        ls_real zC = os_lens_real_focus_z(&s, OS_LINE_C, HUGE_VAL, 0.01);
        NOTE("singlet real focus: F %.4f  d %.4f  C %.4f mm", zF, zd, zC);
        CHECK(zF < zd);
        CHECK(zd < zC);

        /* The achromat pulls F and C back together by a large factor. */
        OsLens a;
        CHECK(os_lens_build(&a, OS_LENS_ACHROMAT_100, 100.0, 8.0, why, sizeof why));
        a.blades = 0;
        CHECK(os_lens_set_fnumber(&a, 8.0));
        ls_real aF = os_lens_real_focus_z(&a, OS_LINE_F, HUGE_VAL, 0.01);
        ls_real aC = os_lens_real_focus_z(&a, OS_LINE_C, HUGE_VAL, 0.01);
        NOTE("F-to-C focus spread: singlet %.4f mm, achromat %.4f mm",
             zC - zF, aC - aF);
        CHECK(fabs(aC - aF) < 0.05 * fabs(zC - zF));
    }

    SECTION("trace: the drawn path is the traced path");
    {
        /* The viewer draws os_lens_trace_path. If it disagreed with
         * os_lens_trace, the picture would be a diagram of a lens that is not
         * the one being rendered -- the most misleading possible bug in a tool
         * whose whole job is to show you what the light did. */
        OsLens L;
        CHECK(os_lens_build(&L, OS_LENS_ACHROMAT_100, 100.0, 8.0, why, sizeof why));
        L.blades = 0;
        CHECK(os_lens_set_fnumber(&L, 8.0));

        for (int i = 0; i <= 12; ++i) {
            ls_real h = L.ep_semi_ap_mm * 1.4 * (ls_real)i / 12.0;
            OsLensRay start = { v3(h, 0.0, -60.0), v3(0.0, 0.0, 1.0) };

            OsLensRay r = start;
            ls_real t = 1.0;
            bool a_ok = os_lens_trace(&L, OS_LINE_D, &r, &t);

            OsRayPath path;
            bool b_ok = os_lens_trace_path(&L, OS_LINE_D, start,
                                           os_lens_vertex_z(&L, L.nsurf - 1)
                                               + L.film_z_mm, &path);
            CHECK(a_ok == b_ok);

            if (a_ok) {
                /* The last surface vertex the path recorded is where the
                 * traced ray ended up. */
                CHECK(path.blocked_at == -1);
                CHECK(path.n >= L.nsurf + 1);
                vec3 at_rear = path.p[L.nsurf];
                CHECK_NEAR(at_rear.x, r.o.x, 1e-12);
                CHECK_NEAR(at_rear.z, r.o.z, 1e-12);
            } else {
                CHECK(path.blocked_at >= 0);
            }
        }
    }
}
