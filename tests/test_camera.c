/* test_camera.c — pupils, camera rays, and the render loop.
 *
 * The claims here are the ones that decide whether an IMAGE is right, as
 * opposed to whether a lens is: that the pupil cache never loses light, that
 * defocus blur matches the closed form, that halving the aperture quarters the
 * exposure, and that the result does not depend on how many threads drew it.
 */
#include "test.h"
#include "tests.h"

#include "opticsim/camera.h"
#include "opticsim/render.h"
#include "opticsim/scenedesc.h"
#include "opticsim/spectral.h"

#include <math.h>
#include <string.h>

/* The textbook thin-lens circle of confusion, for comparison against the
 * general one the lens computes from its own exit pupil. */
static ls_real thin_coc_mm(ls_real A_mm, ls_real f_mm, ls_real focus_mm,
                           ls_real obj_mm) {
    return A_mm * fabs(obj_mm - focus_mm) * f_mm / (obj_mm * (focus_mm - f_mm));
}

void os_test_camera(void) {
    char why[256];

    SECTION("camera: the ideal lens reproduces the textbook blur exactly");
    {
        /* On the ideal thin lens the general expression -- exit pupil, real
         * principal planes, actual film position -- must collapse to the
         * textbook one. If it does not, the difference seen on a real doublet
         * cannot be attributed to the doublet's thickness. */
        OsLens L;
        CHECK(os_lens_build(&L, OS_LENS_THIN, 100.0, 4.0, why, sizeof why));
        CHECK(os_lens_focus(&L, 2.0));

        ls_real A = L.efl_mm / L.f_number;
        for (double obj = 0.5; obj <= 20.0; obj *= 1.5) {
            ls_real got  = os_lens_coc_mm(&L, obj);
            ls_real want = thin_coc_mm(A, L.efl_mm, 2000.0, obj * 1000.0);
            CHECK_NEAR(got, want, 1e-9);
        }

        /* Zero at the focused distance, and nowhere else. */
        CHECK_NEAR(os_lens_coc_mm(&L, 2.0), 0.0, 1e-9);
        CHECK(os_lens_coc_mm(&L, 2.1) > 0.0);
        CHECK(os_lens_coc_mm(&L, 1.9) > 0.0);
    }

    SECTION("camera: blur is proportional to the aperture diameter");
    {
        /* The defining property of depth of field. Checked on the real
         * doublet, where the absolute value differs from the thin-lens formula
         * because the lens is thick -- but the PROPORTIONALITY cannot. */
        OsLens L;
        CHECK(os_lens_build(&L, OS_LENS_ACHROMAT_100, 100.0, 5.0, why, sizeof why));
        CHECK(os_lens_focus(&L, 2.0));

        ls_real ref_c = 0.0, ref_A = 0.0;
        for (double n = 5.0; n <= 32.0; n *= 1.4142135623730951) {
            CHECK(os_lens_set_fnumber(&L, n));
            CHECK(os_lens_focus(&L, 2.0));
            ls_real A = 2.0 * L.ep_semi_ap_mm;
            ls_real c = os_lens_coc_mm(&L, 6.0);
            if (ref_c == 0.0) { ref_c = c; ref_A = A; continue; }
            /* c/A is a constant of the geometry, independent of the stop. */
            CHECK_NEAR(c / A, ref_c / ref_A, 1e-9);
        }

        /* A 6.5 mm doublet at 100 mm focal length is optically thin: its
         * principal planes sit within a millimetre of each other and its
         * pupils are within 0.3 % of the same size, so the general expression
         * and the textbook one agree to a fraction of a percent.
         *
         * Worth asserting rather than assuming. An earlier version of this
         * test claimed a 15 % gap, which came entirely from a sign error in
         * the front focal distance -- the film was being parked for a focus
         * distance it had not been asked for. Two formulas that should agree,
         * and do, is the check that catches that class of bug. */
        CHECK(os_lens_set_fnumber(&L, 5.0));
        CHECK(os_lens_focus(&L, 2.0));
        ls_real thick = os_lens_coc_mm(&L, 6.0);
        ls_real thin  = thin_coc_mm(2.0 * L.ep_semi_ap_mm, L.efl_mm, 2000.0, 6000.0);
        NOTE("6 m blur at f/5: general %.4f mm, textbook %.4f mm (%.2f%% apart)",
             thick, thin, 100.0 * (thick - thin) / thin);
        CHECK_NEAR(thick, thin, 0.01);
    }

    SECTION("camera: the traced spot is the predicted blur, plus aberration");
    {
        /* The closed-form circle of confusion is a PARAXIAL statement. Real
         * rays through real glass land in a slightly bigger spot, and the
         * excess is spherical aberration -- so the test is not that the two
         * agree, it is that they agree in the limit and diverge the right way.
         *
         * Measured by tracing, not by rendering. A rendered blur disc is also
         * widened by the source's own image, by the sampling grid, and by
         * whatever off-axis aberration its position carries; measuring off one
         * conflates four things and can be talked into agreeing with anything. */
        OsLens L;
        CHECK(os_lens_build(&L, OS_LENS_ACHROMAT_100, 100.0, 5.0, why, sizeof why));
        L.blades = 0;

        ls_real prev_excess = 1e30;
        for (double n = 5.0; n <= 32.0; n *= 2.0) {
            CHECK(os_lens_set_fnumber(&L, n));
            CHECK(os_lens_focus(&L, 2.0));

            ls_real zr = os_lens_vertex_z(&L, L.nsurf - 1);
            ls_real zf = zr + L.film_z_mm;
            ls_real lo = 1e30, hi = -1e30;
            int through = 0;

            for (int i = -60; i <= 60; ++i) {
                if (i == 0) continue;
                ls_real hgt = L.ep_semi_ap_mm * (ls_real)i / 60.0;
                vec3 o = v3(0, 0, -6000.0);
                vec3 t = v3(hgt, 0, L.ep_z_mm);
                OsLensRay f = { o, v3norm(v3sub(t, o)) };
                if (!os_lens_trace(&L, OS_LINE_D, &f, NULL)) continue;
                if (fabs(f.d.z) < 1e-15) continue;
                ls_real tt = (zf - f.o.z) / f.d.z;
                ls_real x = f.o.x + tt * f.d.x;
                if (x < lo) lo = x;
                if (x > hi) hi = x;
                through++;
            }
            CHECK(through > 50);

            ls_real traced = hi - lo;
            ls_real want   = os_lens_coc_mm(&L, 6.0);
            ls_real excess = traced / want;
            NOTE("f/%-4.1f traced spot %.4f mm, paraxial %.4f mm, excess %.3f",
                 L.f_number, traced, want, excess);

            /* Bracketed, not pinned. The lower bound is 0.98 rather than 1.0
             * because the spot is measured from a fan of 120 discrete rays,
             * whose outermost pair lands just inside the pupil edge -- about a
             * percent low at small apertures, where there is no aberration
             * left to hide it. */
            CHECK(excess >= 0.98);
            CHECK(excess < 1.20);
            /* And it shrinks as the aperture closes, because spherical
             * aberration goes with the cube of the ray height while the
             * geometric blur only goes linearly. */
            CHECK(excess <= prev_excess + 1e-9);
            prev_excess = excess;
        }
    }

    SECTION("camera: the pupil cache contains the true pupil");
    {
        /* THE pupil invariant. A box that is too small loses light from the
         * corners of every frame, and the result is indistinguishable by eye
         * from correct vignetting -- so it has to be checked by brute force
         * rather than looked at. */
        OsLens L;
        CHECK(os_lens_build(&L, OS_LENS_ACHROMAT_100, 100.0, 5.0, why, sizeof why));
        CHECK(os_lens_focus(&L, 2.0));

        OsPupilCache pc;
        ls_real film_r = 0.5 * sqrt(36.0 * 36.0 + 24.0 * 24.0);
        CHECK(os_pupil_build(&pc, &L, film_r, 32));

        ls_real film_z = os_lens_film_z(&L);
        ls_real rear_semi = L.surf[L.nsurf - 1].semi_ap_mm;
        int escaped = 0, tested = 0;

        /* Probed at three wavelengths, though the cache was built at one: the
         * pupil shifts slightly with dispersion and the padding has to absorb
         * that too. */
        const ls_real LAMS[3] = { 400.0, 550.0, 700.0 };
        for (int li = 0; li < 3; ++li)
        for (int zi = 0; zi <= 10; ++zi) {
            ls_real fr = film_r * (ls_real)zi / 10.0;
            vec3 film = v3(fr, 0.0, film_z);
            OsRect b = os_pupil_bounds(&pc, fr);

            for (int iy = 0; iy < 24; ++iy)
                for (int ix = 0; ix < 24; ++ix) {
                    ls_real rx = -rear_semi + 2.0 * rear_semi * ((ls_real)ix + 0.5) / 24.0;
                    ls_real ry = -rear_semi + 2.0 * rear_semi * ((ls_real)iy + 0.5) / 24.0;
                    vec3 target = v3(rx, ry, pc.rear_z_mm);
                    OsLensRay r = { film, v3norm(v3sub(target, film)) };
                    if (!os_lens_trace_reverse(&L, LAMS[li], &r, NULL)) continue;

                    tested++;
                    /* This point DID get through, so the cached box must
                     * contain it. */
                    bool inside = rx >= b.x0 - 1e-9 && rx <= b.x1 + 1e-9
                               && ry >= b.y0 - 1e-9 && ry <= b.y1 + 1e-9;
                    if (!inside) escaped++;
                }
        }
        NOTE("pupil: %d points got through, %d fell outside the cached box",
             tested, escaped);
        CHECK(tested > 1000);
        CHECK(escaped == 0);
        os_pupil_free(&pc);
    }

    SECTION("camera: closing down one stop quarters the light");
    {
        /* The half of the exposure triangle that exists before any sensor
         * does. Measured on the film, in physical units, with noise off --
         * because there is no noise yet. */
        OsStage st;
        OsSceneDesc desc;
        os_scenedesc_preset(&desc, OS_STAGE_DEPTH_RAIL);
        CHECK(os_scenedesc_build(&desc, &st));

        const int W = 48, H = 32;
        ls_real energy[2];
        const double stops[2] = { 5.0, 10.0 };

        for (int k = 0; k < 2; ++k) {
            OsCamera cam;
            CHECK(os_camera_build(&cam, OS_LENS_ACHROMAT_100, 100.0, stops[k],
                                  36.0, W, H, why, sizeof why));
            CHECK(os_lens_focus(&cam.lens, 2.0));
            os_camera_refresh(&cam);
            os_camera_look_at(&cam, st.cam_eye, st.cam_target, v3(0, 1, 0));

            Film film;
            CHECK(ls_film_init(&film, W, H));
            /* 384 samples a pixel, and the count is load-bearing. The rail's
             * targets are saturated hues now rather than near-neutral greys,
             * and Smits' uplift gives a saturated reflectance a far more
             * structured spectrum -- so a hero-wavelength estimator has more
             * variance per sample on this scene than it used to. At 96 spp the
             * ratio below lands around 3.81, which is noise and reads as a
             * broken exposure claim. */
            OsRenderOpts opt = { 384, 4, 0, 0x853C49E6748FEA9Bull };
            os_render_pass(&film, &cam, &st, &opt, 0);

            /* The CENTRE of the frame only.
             *
             * Summed over the whole frame the ratio comes out near 3.9, not 4,
             * and that is not noise -- it is mechanical vignetting. At f/5 the
             * corners lose light to the edges of the glass; by f/10 the iris is
             * inside every clear aperture and they do not. So the full-frame
             * ratio is genuinely below four, and asserting four there would be
             * asserting that vignetting does not exist. On axis there is
             * nothing to clip and the pupil area is the whole story. */
            ls_real sum = 0.0;
            for (int y = H / 2 - 3; y <= H / 2 + 3; ++y)
                for (int x = W / 2 - 3; x <= W / 2 + 3; ++x) {
                    Spectrum s = ls_film_mean(&film, x, y);
                    sum += ls_spectrum_integrate(&s);
                }
            energy[k] = sum;
            ls_film_free(&film);
            os_camera_free(&cam);
        }

        ls_real ratio = energy[0] / energy[1];
        NOTE("f/5 vs f/10 on-axis film energy: ratio %.3f (want 4.000)", ratio);
        /* Exactly four, up to Monte Carlo noise: the pupil AREA halves twice.
         * 2 % is the sampling noise at this size and sample count, not slack in
         * the claim -- it lands within 0.5 % here and keeps closing with more
         * samples (0.9 % at 768, 0.9 % at 1536, wandering rather than biased). */
        CHECK_NEAR(ratio, 4.0, 0.02);

        os_stage_free(&st);
    }

    SECTION("camera: the image does not depend on the thread count");
    {
        /* The vendored ls_parallel_for seeds from the WORK-ITEM index, never
         * from a thread id, and this is the test that defends it. It is the
         * first property to break silently, and once broken every other
         * numeric test in this file becomes flaky rather than false. */
        OsStage st;
        OsSceneDesc desc;
        os_scenedesc_preset(&desc, OS_STAGE_DEPTH_RAIL);
        CHECK(os_scenedesc_build(&desc, &st));

        const int W = 40, H = 28;
        OsCamera cam;
        CHECK(os_camera_build(&cam, OS_LENS_ACHROMAT_100, 100.0, 5.0,
                              36.0, W, H, why, sizeof why));
        CHECK(os_lens_focus(&cam.lens, 2.0));
        os_camera_refresh(&cam);
        os_camera_look_at(&cam, st.cam_eye, st.cam_target, v3(0, 1, 0));

        Film a, b;
        CHECK(ls_film_init(&a, W, H));
        CHECK(ls_film_init(&b, W, H));

        OsRenderOpts one   = { 16, 4, 1, 0x853C49E6748FEA9Bull };
        OsRenderOpts eight = { 16, 4, 8, 0x853C49E6748FEA9Bull };
        os_render_pass(&a, &cam, &st, &one,   0);
        os_render_pass(&b, &cam, &st, &eight, 0);

        /* Bit for bit, not approximately. */
        size_t np = (size_t)W * (size_t)H;
        CHECK(memcmp(a.pix, b.pix, np * sizeof *a.pix) == 0);
        CHECK(memcmp(a.n,   b.n,   np * sizeof *a.n) == 0);

        ls_film_free(&a);
        ls_film_free(&b);
        os_camera_free(&cam);
        os_stage_free(&st);
    }

    SECTION("camera: a camera renders before its pupil cache is built");
    {
        /* THE safety property behind making the scan lazy.
         *
         * os_camera_build used to run the full ~74 000-trace pupil scan, and
         * every caller then changed the focus and ran it again -- the first was
         * always thrown away. Dropping it is only safe because a camera with no
         * table still samples correctly: os_pupil_bounds falls back to the
         * whole rear element, which is the loosest bound that still CONTAINS
         * the pupil, so rays are wasted and none are lost. Getting that wrong
         * would not be slow, it would be black.
         *
         * So: the same frame twice, once without the table and once with, and
         * they have to agree on how much light arrived. Not bit for bit -- the
         * two samplers draw different points from the same stream -- but well
         * inside what 64 samples a pixel can tell apart. */
        OsStage st;
        OsSceneDesc desc;
        os_scenedesc_preset(&desc, OS_STAGE_DEPTH_RAIL);
        CHECK(os_scenedesc_build(&desc, &st));

        const int W = 32, H = 22;
        ls_real total[2];

        for (int k = 0; k < 2; ++k) {
            OsCamera cam;
            CHECK(os_camera_build(&cam, OS_LENS_ACHROMAT_100, 100.0, 5.0,
                                  36.0, W, H, why, sizeof why));
            CHECK(os_lens_focus(&cam.lens, 2.0));
            if (k == 1) os_camera_refresh(&cam);      /* the cached one */
            else        CHECK(cam.pupil.nzones == 0); /* the trivial one */
            os_camera_look_at(&cam, st.cam_eye, st.cam_target, v3(0, 1, 0));

            Film f;
            CHECK(ls_film_init(&f, W, H));
            OsRenderOpts opt = { 256, 4, 0, 0x853C49E6748FEA9Bull };
            os_render_pass(&f, &cam, &st, &opt, 0);

            ls_real sum = 0.0;
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    Spectrum sp = ls_film_mean(&f, x, y);
                    sum += ls_spectrum_integrate(&sp);
                }
            total[k] = sum;

            ls_film_free(&f);
            os_camera_free(&cam);
        }

        NOTE("film total: %.6g without the pupil table, %.6g with it (%.2f%%)",
             total[0], total[1], 100.0 * (total[0] / total[1] - 1.0));
        /* Both lit -- the black-frame failure this guards against. */
        CHECK(total[0] > 0.0);
        CHECK(total[1] > 0.0);
        /* And the same picture, to within the noise -- which is what says the
         * looser sampler is UNBIASED rather than merely bright enough. The
         * gap shrinks with the sample count the way noise does and a bias
         * would not: 2.1 % at 64 samples a pixel, 0.5 % at 256, 0.11 % at
         * 1024. The tolerance here is set for 256. */
        CHECK_NEAR(total[0], total[1], 0.01);

        os_stage_free(&st);
    }

    SECTION("spectral: the hero wavelength covers the band without clumping");
    {
        /* Every bin must be reachable, and a short run of samples must spread
         * across the band rather than revisiting a few wavelengths -- that
         * spread is what keeps chroma noise down. */
        int count[LS_NBINS];
        memset(count, 0, sizeof count);

        uint32_t hash = os_pixel_hash(17, 23);
        const int N = LS_NBINS * 40;
        for (int i = 0; i < N; ++i) {
            OsWavelength w = os_lambda_pick((uint64_t)i, hash);
            CHECK(w.bin >= 0 && w.bin < LS_NBINS);
            /* The wavelength is exactly a bin centre, so reading a Spectrum
             * back at it is exact. */
            CHECK(w.lambda_nm == ls_bin_lambda(w.bin));
            CHECK(w.inv_pdf > 0.0);
            count[w.bin]++;
        }
        int lo = N, hi = 0;
        for (int i = 0; i < LS_NBINS; ++i) {
            if (count[i] < lo) lo = count[i];
            if (count[i] > hi) hi = count[i];
        }
        NOTE("over %d draws each of %d bins got between %d and %d",
             N, LS_NBINS, lo, hi);
        CHECK(lo > 0);                 /* no bin is unreachable */
        /* With N/95 = 40 expected per bin, independent uniform draws would
         * scatter by about +/-3 sigma = +/-19. The additive recurrence holds
         * every bin within a few counts of the mean, which is the whole reason
         * it is used instead of ls_rng_f. */
        CHECK(hi - lo <= 5);

        /* Different pixels walk the band in different orders, or the residual
         * noise becomes a visible pattern rather than grain. */
        int same = 0;
        for (int i = 0; i < 64; ++i) {
            OsWavelength p = os_lambda_pick((uint64_t)i, os_pixel_hash(1, 1));
            OsWavelength q = os_lambda_pick((uint64_t)i, os_pixel_hash(2, 1));
            if (p.bin == q.bin) same++;
        }
        CHECK(same < 32);
    }
}
