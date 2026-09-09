/* test_lens.c — first-order optics, against closed forms.
 *
 * The two results that matter most in this file are the thin-lens limit and
 * the achromatic condition. Together they prove that the glass table, the
 * paraxial trace and the doublet derivation are ALL simultaneously right,
 * because each one would break them in a different, visible way:
 *
 *   - wrong Sellmeier data      -> the achromat's residual colour is not small
 *   - wrong y-nu implementation -> the thin limit is not 100.000 mm
 *   - wrong doublet derivation  -> the singlet and doublet behave alike
 *
 * No single check can distinguish those; the pair of them can. */
#include "test.h"
#include "tests.h"

#include "opticsim/lens.h"

#include <math.h>
#include <string.h>

/* Collapse a lens to zero thickness, so the thin-lens formulae apply exactly.
 * The design equations that built these prescriptions are thin-lens formulae,
 * so this is the only configuration in which they can be checked to the last
 * bit; with real 4 mm and 2.5 mm elements the focal length shifts by a fraction
 * of a percent, which is physics, not error. */
static void collapse(OsLens *L) {
    for (int i = 0; i < L->nsurf; ++i) L->surf[i].thickness_mm = 0.0;
    os_lens_paraxial(L, OS_LINE_D, &L->efl_mm, &L->bfd_mm, &L->pp_rear_mm);
}

void os_test_lens(void) {
    char why[256];

    SECTION("lens: every shipped prescription builds");
    {
        /* os_lens_build refuses when the paraxial focal length disagrees with
         * the design value, so this is the transcription gate firing. */
        for (int id = 0; id < OS_LENS_COUNT; ++id) {
            OsLens L;
            bool ok = os_lens_build(&L, (OsPrescriptionId)id, 0.0, 0.0,
                                    why, sizeof why);
            if (!ok) NOTE("%s: %s", os_prescription_name((OsPrescriptionId)id), why);
            CHECK(ok);
        }
    }

    SECTION("lens: the ideal thin lens is ideal");
    {
        /* It has to be exactly 100 mm at EVERY wavelength, because it is what
         * the exposure tests will use: f/2 at 1/500 s must equal f/2.8 at
         * 1/250 s to the last bit, and on any real prescription it does not,
         * because changing the aperture also changes the vignetting and the
         * aberration. An optic with a residue of either would force those
         * tolerances open until the test could no longer see a real error. */
        OsLens t;
        CHECK(os_lens_build(&t, OS_LENS_THIN, 0.0, 0.0, why, sizeof why));
        CHECK_NEAR(t.efl_mm, 100.0, 1e-12);
        CHECK_NEAR(t.bfd_mm, 100.0, 1e-12);
        CHECK_NEAR(t.pp_rear_mm, 0.0, 1e-12);

        /* Zero dispersion: F, d and C focus at the same place exactly. */
        CHECK(os_lens_efl_at(&t, OS_LINE_F) == os_lens_efl_at(&t, OS_LINE_C));
        for (ls_real l = 380.0; l <= 780.0; l += 10.0)
            CHECK_NEAR(os_lens_efl_at(&t, l), 100.0, 1e-12);

        /* And it scales like anything else. */
        CHECK(os_lens_build(&t, OS_LENS_THIN, 35.0, 2.0, why, sizeof why));
        CHECK_NEAR(t.efl_mm, 35.0, 1e-12);
        CHECK_NEAR(t.f_number, 2.0, 1e-12);
        CHECK_NEAR(2.0 * t.ep_semi_ap_mm, 17.5, 1e-12);
    }

    SECTION("lens: the thin-lens limit is exactly 100 mm");
    {
        /* The y-nu trace is an independent computation from the design
         * equations that produced the radii -- one walks surfaces, the other
         * is a closed form -- so their agreement is a real check, not a
         * tautology. */
        OsLens s, a;
        CHECK(os_lens_build(&s, OS_LENS_SINGLET_100,  0.0, 0.0, why, sizeof why));
        CHECK(os_lens_build(&a, OS_LENS_ACHROMAT_100, 0.0, 0.0, why, sizeof why));

        collapse(&s);
        collapse(&a);
        CHECK_NEAR(os_lens_efl_at(&s, OS_LINE_D), 100.0, 1e-9);
        CHECK_NEAR(os_lens_efl_at(&a, OS_LINE_D), 100.0, 1e-9);

        /* At zero thickness the rear principal plane coincides with the
         * vertex, so BFD and EFL are the same number. */
        CHECK_NEAR(s.bfd_mm, 100.0, 1e-9);
        CHECK_NEAR(a.bfd_mm, 100.0, 1e-9);
        CHECK_NEAR(s.pp_rear_mm, 0.0, 1e-9);
    }

    SECTION("lens: the singlet's colour error is exactly -1/V_d");
    {
        /* A thin equiconvex singlet has f(lambda) = R / (2(n(lambda) - 1)), so
         * its focal length is exactly inversely proportional to (n - 1) and
         *
         *     (f_F - f_C)/f_d = (n_d - 1)(n_C - n_F) / ((n_F - 1)(n_C - 1))
         *
         * EXACTLY, with no approximation anywhere. That is what is asserted
         * here, to 1e-12, and it is a tighter statement than the textbook
         * -1/V_d -- which is this same expression with (n_F - 1)(n_C - 1)
         * replaced by (n_d - 1)^2, and is therefore only good to about 0.6 %.
         * Asserting the approximation to 1e-9 would fail correct code; loosening
         * to 1 % would stop the test seeing a real error. So: assert the exact
         * form tightly, and check the approximation to its own accuracy. */
        OsLens s;
        CHECK(os_lens_build(&s, OS_LENS_SINGLET_100, 0.0, 0.0, why, sizeof why));
        collapse(&s);

        ls_real fF = os_lens_efl_at(&s, OS_LINE_F);
        ls_real fd = os_lens_efl_at(&s, OS_LINE_D);
        ls_real fC = os_lens_efl_at(&s, OS_LINE_C);
        ls_real rel = (fF - fC) / fd;

        const OsGlass *bk7 = os_glass(OS_GLASS_N_BK7);
        ls_real nF = os_glass_n(bk7, OS_LINE_F);
        ls_real nd = os_glass_n(bk7, OS_LINE_D);
        ls_real nC = os_glass_n(bk7, OS_LINE_C);
        ls_real exact = (nd - 1.0) * (nC - nF) / ((nF - 1.0) * (nC - 1.0));

        ls_real vd = os_glass_abbe(bk7);
        NOTE("singlet (f_F-f_C)/f_d = %.9f   exact %.9f   -1/V_d %.9f",
             rel, exact, -1.0 / vd);
        CHECK_NEAR(rel, exact, 1e-12);
        CHECK_NEAR(rel, -1.0 / vd, 1e-2);   /* the approximation, to its worth */

        /* Blue focuses SHORT of red. The sign is the whole phenomenon: get it
         * backwards and the simulated fringing would appear on the wrong side
         * of focus, which looks plausible and is wrong. */
        CHECK(fF < fd);
        CHECK(fd < fC);
    }

    SECTION("lens: the achromat kills two orders of magnitude of that");
    {
        OsLens a;
        CHECK(os_lens_build(&a, OS_LENS_ACHROMAT_100, 0.0, 0.0, why, sizeof why));
        collapse(&a);

        ls_real fF = os_lens_efl_at(&a, OS_LINE_F);
        ls_real fd = os_lens_efl_at(&a, OS_LINE_D);
        ls_real fC = os_lens_efl_at(&a, OS_LINE_C);
        ls_real rel = fabs((fF - fC) / fd);

        OsLens s;
        CHECK(os_lens_build(&s, OS_LENS_SINGLET_100, 0.0, 0.0, why, sizeof why));
        collapse(&s);
        ls_real srel = fabs((os_lens_efl_at(&s, OS_LINE_F)
                           - os_lens_efl_at(&s, OS_LINE_C)) / os_lens_efl_at(&s, OS_LINE_D));

        NOTE("achromat |f_F-f_C|/f_d = %.3e   singlet %.3e", rel, srel);

        /* At the thin limit the residual is not merely small, it is EXACTLY
         * zero, and the derivation says why. The combined power is
         *
         *     phi(lambda) = (n1-1)(2/R1) + (n2-1)(1/R2 - 1/R3)
         *
         * and the radii were chosen so those bracketed factors are phi1/(n1d-1)
         * and phi2/(n2d-1). So
         *
         *     phi_F - phi_C = dn1 phi1/(n1d-1) + dn2 phi2/(n2d-1)
         *                   = phi1/V1 + phi2/V2
         *
         * which the Fraunhofer split sets to zero identically -- not to first
         * order, identically. Anything above rounding here means the glass
         * data, the power split or the radii disagree with each other.
         *
         * Compared ABSOLUTELY, not relatively: a relative tolerance against an
         * expected value of zero is meaningless. */
        CHECK(rel < 1e-12);

        /* The gap versus the singlet is the actual claim, and it is asserted
         * as a difference rather than the ratio srel/rel -- which would be a
         * division by the zero just established. */
        CHECK(srel > 1e-3);
        CHECK(srel > 1e6 * rel + 1e-9);

        /* The secondary spectrum is real: an achromat matches F to C but the
         * d line still focuses slightly differently. It must not be zero, or
         * the model is too good to be true. */
        CHECK(fabs(fd - fF) > 0.0);
    }

    SECTION("lens: uniform scaling is exact and preserves the f-number");
    {
        /* The focal-length control is prescription scaling, so this is the
         * test that says dragging that control produces real designs. */
        const double ks[] = { 0.5, 1.0, 2.0, 3.7 };
        OsLens base;
        CHECK(os_lens_build(&base, OS_LENS_ACHROMAT_100, 100.0, 8.0,
                            why, sizeof why));
        for (int i = 0; i < 4; ++i) {
            OsLens L;
            ls_real want = 100.0 * ks[i];
            /* f/8, not f/4: this doublet's front element is 20 mm across at
             * f = 100 mm, so f/4 would need a 25 mm entrance pupil and gets
             * clamped to the glass. That clamp is correct behaviour and is
             * tested separately; asking for it here would be testing the
             * clamp, not the scaling. */
            CHECK(os_lens_build(&L, OS_LENS_ACHROMAT_100, want, 8.0,
                                why, sizeof why));

            /* The requested focal length is delivered EXACTLY -- ask for 85 mm
             * and get 85 mm, not the 84.7 mm that scaling by the design's
             * nominal value would have produced. */
            CHECK_NEAR(L.efl_mm, want, 1e-12);

            /* Every angle is untouched, so the f-number is preserved exactly
             * and the entrance pupil scales with the focal length. */
            CHECK_NEAR(L.f_number, 8.0, 1e-12);
            CHECK_NEAR(L.ep_semi_ap_mm, want / 16.0, 1e-9);

            /* Lengths scale together... */
            CHECK_NEAR(L.total_track_mm, base.total_track_mm * ks[i], 1e-9);
            CHECK_NEAR(L.bfd_mm,         base.bfd_mm         * ks[i], 1e-9);
            CHECK_NEAR(L.image_circle_mm, base.image_circle_mm * ks[i], 1e-9);

            /* ...and the chromatic ratio, being dimensionless, does not move
             * at all. That is the claim that scaling preserves the aberration
             * CHARACTER and not merely the focal length. */
            ls_real r1 = (os_lens_efl_at(&L, OS_LINE_F)
                        - os_lens_efl_at(&L, OS_LINE_C)) / os_lens_efl_at(&L, OS_LINE_D);
            ls_real r0 = (os_lens_efl_at(&base, OS_LINE_F)
                        - os_lens_efl_at(&base, OS_LINE_C)) / os_lens_efl_at(&base, OS_LINE_D);
            CHECK_NEAR(r1, r0, 1e-12);
        }
    }

    SECTION("lens: the f-number sets the entrance pupil, not the stop");
    {
        OsLens L;
        CHECK(os_lens_build(&L, OS_LENS_ACHROMAT_100, 100.0, 2.0, why, sizeof why));

        /* The defining relation: entrance pupil diameter = focal length / N.
         * Swept from the design's widest (f/5) to well stopped down. */
        for (double n = 5.0; n <= 32.0; n *= 1.4142135623730951) {
            CHECK(os_lens_set_fnumber(&L, n));
            CHECK_NEAR(2.0 * L.ep_semi_ap_mm, L.efl_mm / n, 1e-9);
            CHECK_NEAR(L.f_number, n, 1e-12);
        }

        /* Asking for more light than the glass can pass clamps to the
         * mechanical limit AND reports the f-number actually achieved. A lens
         * that claimed f/1.4 while passing f/5 would make every exposure
         * computed from it wrong by that ratio, with nothing in the image to
         * show for it. */
        CHECK(os_lens_set_fnumber(&L, 1.4));
        CHECK(L.f_number > 1.4);
        CHECK_NEAR(L.stop_semi_ap_mm, L.surf[L.stop_index].semi_ap_mm, 1e-12);
        CHECK_NEAR(L.f_number, L.efl_mm / (2.0 * L.ep_semi_ap_mm), 1e-12);

        /* On this doublet the stop IS the front surface, so the pupil
         * magnification is exactly 1 and stop radius equals pupil radius.
         * Asserting that here pins the trivial case, so a future design with
         * glass ahead of its stop can be compared against it. */
        CHECK_NEAR(L.ep_mag, 1.0, 1e-12);
        CHECK_NEAR(L.ep_z_mm, 0.0, 1e-12);

        /* Nonsense refused rather than clamped silently. */
        CHECK(!os_lens_set_fnumber(&L, 0.0));
        CHECK(!os_lens_set_fnumber(&L, -2.0));
    }

    SECTION("lens: focus moves the film the right way");
    {
        OsLens L;
        CHECK(os_lens_build(&L, OS_LENS_ACHROMAT_100, 100.0, 4.0, why, sizeof why));

        /* At infinity the film sits at the back focal distance. That is the
         * definition of BFD, and it is the point where the conjugate equation
         * and the y-nu trace have to agree. */
        CHECK(os_lens_focus(&L, HUGE_VAL));
        CHECK_NEAR(L.film_z_mm, L.bfd_mm, 1e-12);

        /* Closer focus extends the lens: the film must move AWAY from the
         * glass, never toward it. A sign error here would make the lens focus
         * closer as you racked it toward infinity. */
        ls_real prev = L.film_z_mm;
        const double dists[] = { 100.0, 10.0, 5.0, 2.0, 1.0, 0.5 };
        for (int i = 0; i < 6; ++i) {
            CHECK(os_lens_focus(&L, dists[i]));
            CHECK(L.film_z_mm > prev);
            prev = L.film_z_mm;
        }

        /* At 1:1 magnification -- object two focal lengths away from the front
         * principal plane -- the image sits two focal lengths behind the rear
         * one. The classic conjugate pair, in millimetres. */
        ls_real two_f_m = (2.0 * L.efl_mm + L.ffd_mm + L.efl_mm) / 1000.0;
        CHECK(os_lens_focus(&L, two_f_m));
        CHECK_NEAR(L.film_z_mm - L.pp_rear_mm, 2.0 * L.efl_mm, 1e-6);

        /* An object at or inside the front focal point cannot be imaged. */
        CHECK(!os_lens_focus(&L, 0.0));
        CHECK(!os_lens_focus(&L, -1.0));
    }

    SECTION("lens: transmittance is below one and falls with more glass");
    {
        OsLens s, a;
        CHECK(os_lens_build(&s, OS_LENS_SINGLET_100,  0.0, 0.0, why, sizeof why));
        CHECK(os_lens_build(&a, OS_LENS_ACHROMAT_100, 0.0, 0.0, why, sizeof why));

        ls_real ts = os_lens_transmittance(&s, OS_LINE_D);
        ls_real ta = os_lens_transmittance(&a, OS_LINE_D);
        NOTE("uncoated transmittance: singlet %.4f, achromat %.4f", ts, ta);

        CHECK(ts > 0.0 && ts < 1.0);
        CHECK(ta > 0.0 && ta < ts);      /* more interfaces, less light */

        /* Two air-glass interfaces of N-BK7 at normal incidence. Fresnel gives
         * R = ((n-1)/(n+1))^2 = 0.042150 for n = 1.51680, so the singlet must
         * transmit (1 - R)^2 exactly. */
        ls_real n = os_glass_n(os_glass(OS_GLASS_N_BK7), OS_LINE_D);
        ls_real R = ((n - 1.0) / (n + 1.0)) * ((n - 1.0) / (n + 1.0));
        CHECK_NEAR(R, 0.0421646, 1e-5);
        CHECK_NEAR(ts, (1.0 - R) * (1.0 - R), 1e-9);
    }

    SECTION("lens: field of view follows the focal length");
    {
        OsLens L;
        const ls_real ff_diag = 43.267;    /* 36 x 24 mm full frame */

        /* The textbook pairs, to the nearest tenth of a degree. If the focal
         * length were being scaled wrongly, these would drift together and
         * stay self-consistent -- so they are checked against literals. */
        struct { double f, half_deg; } want[] = {
            {  24.0, 42.0 }, {  50.0, 23.4 }, {  85.0, 14.3 }, { 200.0, 6.2 },
        };
        for (int i = 0; i < 4; ++i) {
            CHECK(os_lens_build(&L, OS_LENS_ACHROMAT_100, want[i].f, 4.0,
                                why, sizeof why));
            CHECK_NEAR(os_lens_half_fov_deg(&L, ff_diag), want[i].half_deg, 5e-3);
        }
    }
}
