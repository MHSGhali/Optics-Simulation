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

        /* Zero dispersion: F, d and C focus at the same place exactly. */
        CHECK(os_lens_efl_at(&t, OS_LINE_F) == os_lens_efl_at(&t, OS_LINE_C));
        for (ls_real l = 380.0; l <= 780.0; l += 10.0)
            CHECK_NEAR(os_lens_efl_at(&t, l), 100.0, 1e-12);

        /* ---- and it is 20 mm long, which the numbers below are about ----
         *
         * The design's two surfaces used to share a vertex, and the sphere's
         * cap bulges 13.4 mm past that vertex at the full clear aperture, so
         * the plano sat inside the sphere and every traced ray came back
         * vignetted. See build_thin. They are 20 mm apart now, and these two
         * numbers are the entire optical price of the gap:
         *
         *   BACK FOCAL DISTANCE   100 -> 90
         *   REAR PRINCIPAL PLANE    0 -> -10
         *
         * Both are POSITIONS. The power is untouched -- a plano contributes
         * none at any thickness -- which is why the focal length above is
         * still exact to the last bit at every wavelength, and why the circle
         * of confusion is unchanged too: the rear principal plane and the exit
         * pupil each move back by the same 10 mm, and the lever between them
         * is what a blur is measured on. test_camera's textbook comparison
         * asserts that at 1e-9 and did not need touching. */
        CHECK_NEAR(t.bfd_mm, 90.0, 1e-12);
        CHECK_NEAR(t.pp_rear_mm, -10.0, 1e-12);

        /* Collapse the gap and the textbook thin lens is exactly what is left,
         * which is the claim the two numbers above would otherwise obscure. */
        OsLens flat = t;
        collapse(&flat);
        CHECK_NEAR(flat.efl_mm, 100.0, 1e-12);
        CHECK_NEAR(flat.bfd_mm, 100.0, 1e-12);
        CHECK_NEAR(flat.pp_rear_mm, 0.0, 1e-12);

        /* And it scales like anything else. */
        CHECK(os_lens_build(&t, OS_LENS_THIN, 35.0, 2.0, why, sizeof why));
        CHECK_NEAR(t.efl_mm, 35.0, 1e-12);
        CHECK_NEAR(t.f_number, 2.0, 1e-12);
        CHECK_NEAR(2.0 * t.ep_semi_ap_mm, 17.5, 1e-12);
        /* The gap scales with everything else, so the BFD stays 0.9 f. */
        CHECK_NEAR(t.bfd_mm, 31.5, 1e-12);
    }

    SECTION("lens: every shipped design actually passes light");
    {
        /* THE test the ideal lens needed and did not have.
         *
         * Its two surfaces shared a vertex while the first bulged 13.4 mm past
         * it, so a sequential trace -- which visits surfaces in prescription
         * order, not in hit order -- reached the second one having already
         * flown through where the first was, and found it behind itself. Every
         * ray came back vignetted and the design rendered pure BLACK, from the
         * CLI and in the viewer both.
         *
         * It survived for as long as it did because every test here was
         * paraxial: the y-nu trace walks surfaces arithmetically and never asks
         * where they are. So this one traces real rays, in both directions,
         * through every shipped design -- the cheapest possible statement that
         * a lens is a lens and not a wall. */
        for (int id = 0; id < OS_LENS_COUNT; ++id) {
            OsLens L;
            CHECK(os_lens_build(&L, (OsPrescriptionId)id, 100.0, 5.0,
                                why, sizeof why));
            CHECK(os_lens_focus(&L, 2.0));

            ls_real film_z = os_lens_film_z(&L);
            ls_real rear_z = os_lens_vertex_z(&L, L.nsurf - 1);
            int out = 0, in = 0;
            const int N = 40;
            for (int i = 0; i < N; ++i) {
                /* Across the pupil, stopping short of its rim so the count is
                 * about the geometry rather than about the clip. */
                ls_real h = ((ls_real)i / (ls_real)N) * L.ep_semi_ap_mm * 0.9;

                OsLensRay r = { v3(0.0, 0.0, film_z),
                                v3norm(v3sub(v3(h, 0.0, rear_z),
                                             v3(0.0, 0.0, film_z))) };
                if (os_lens_trace_reverse(&L, OS_LINE_D, &r, NULL)) out++;

                /* And the other way, from a subject at 2 m toward the pupil. */
                vec3 o = v3(0.0, 0.0, -2000.0);
                OsLensRay f = { o, v3norm(v3sub(v3(h, 0.0, L.ep_z_mm), o)) };
                if (os_lens_trace(&L, OS_LINE_D, &f, NULL)) in++;
            }
            NOTE("%s: %d/%d rays out, %d/%d in",
                 os_prescription_name((OsPrescriptionId)id), out, N, in, N);
            CHECK(out > N / 2);
            CHECK(in  > N / 2);
        }
    }

    SECTION("lens: the ideal design is ideal in COLOUR, not in its rays");
    {
        /* Two comments and a user-visible string used to call it
         * aberration-free. It is a single spherical surface, which is not
         * aplanatic: at f/5 it leaves MORE spherical aberration than the
         * achromat, whose second element bends the marginal rays back. What it
         * really guarantees is an exact focal length and no colour, and the
         * difference matters to anyone choosing it to look at a lens's best
         * case. Pinned so the claim cannot come back. */
        OsLens ideal, achromat;
        CHECK(os_lens_build(&ideal, OS_LENS_THIN, 100.0, 5.0, why, sizeof why));
        CHECK(os_lens_build(&achromat, OS_LENS_ACHROMAT_100, 100.0, 5.0,
                            why, sizeof why));
        CHECK(os_lens_focus(&ideal, 2.0));
        CHECK(os_lens_focus(&achromat, 2.0));

        ls_real si = os_lens_spot_mm(&ideal, 2.0, 0.0, 21);
        ls_real sa = os_lens_spot_mm(&achromat, 2.0, 0.0, 21);
        NOTE("on axis at f/5: ideal %.4f mm, achromat %.4f mm", si, sa);
        CHECK(isfinite(si) && isfinite(sa));
        CHECK(si > sa);

        /* And stopping down cleans it up, which is what identifies the cause
         * as SPHERICAL aberration rather than as anything chromatic -- the
         * design has no colour to correct. */
        OsLens stopped;
        CHECK(os_lens_build(&stopped, OS_LENS_THIN, 100.0, 16.0,
                            why, sizeof why));
        CHECK(os_lens_focus(&stopped, 2.0));
        ls_real ss = os_lens_spot_mm(&stopped, 2.0, 0.0, 21);
        NOTE("the same design at f/16: %.4f mm", ss);
        CHECK(ss < si / 10.0);
    }

    SECTION("lens: a design reports the widest it can actually open");
    {
        /* The number the OPEN control needs and did not have. The viewer used
         * to hard-code a floor of f/1 and let the aperture walk past the point
         * where the iris is already against the bore -- on the achromat, which
         * is wide open a hair inside the f/5 the viewer starts on, so the
         * first thing anyone tried moved a number and changed no photograph.
         *
         * The achromat's stop is its front surface with nothing in front of it
         * to magnify, so the widest pupil is that surface's clear aperture and
         * the limit is the focal length over its diameter.
         *
         * NEAR f/5 and not exactly f/5, which is worth knowing: the table says
         * 10 mm of semi-aperture at a design focal length of 100 mm, but
         * os_lens_build rescales every length so the MEASURED paraxial focal
         * length is 100 -- and the design equations are thin-lens ones, so a
         * real doublet with 4 mm and 2.5 mm elements comes out 0.36 % short
         * and its aperture is scaled up with it. The design's nominal
         * f-number is a label; this is the aperture. */
        OsLens L;
        CHECK(os_lens_build(&L, OS_LENS_ACHROMAT_100, 100.0, 5.0,
                            why, sizeof why));
        ls_real widest = os_lens_min_fnumber(&L);
        NOTE("the achromat is wide open at f/%.4f, against a nominal f/5",
             widest);
        CHECK(widest > 4.9 && widest < 5.0);
        CHECK_NEAR(widest,
                   L.efl_mm / (2.0 * L.surf[L.stop_index].semi_ap_mm), 1e-12);

        /* And it is the value the clamp actually enforces, which is the claim
         * that matters -- the two are computed by different code. */
        CHECK(os_lens_set_fnumber(&L, 1.4));
        CHECK_NEAR(L.f_number, widest, 1e-12);

        /* Scale-invariant: every length moves together, so the ratio does not.
         * A 200 mm achromat is exactly as fast as a 100 mm one. */
        CHECK(os_lens_build(&L, OS_LENS_ACHROMAT_100, 200.0, 5.0,
                            why, sizeof why));
        CHECK_NEAR(os_lens_min_fnumber(&L), widest, 1e-12);

        /* Stopping down is never clamped, so the report is unchanged by it. */
        CHECK(os_lens_set_fnumber(&L, 22.0));
        CHECK_NEAR(L.f_number, 22.0, 1e-12);
        CHECK_NEAR(os_lens_min_fnumber(&L), widest, 1e-12);
    }

    SECTION("lens: a zoom changes its design, where the others are scaled");
    {
        /* THE distinction this design exists to make.
         *
         * Every other prescription here reaches another focal length by
         * SCALING -- multiply every length by k and you have a real lens of
         * the same form, with every angle unchanged and therefore every
         * aberration identical. That is a genuine optical operation, and it is
         * also why a scaled design's character never moves however far the
         * control is dragged.
         *
         * A zoom is not that. Its groups sit at a separation, every separation
         * is a different lens, and the aberrations move because the design
         * moved. The checks below are that claim, stated four ways. */
        char why[256];
        static const double F[4] = { 45.0, 60.0, 80.0, 100.0 };
        ls_real track[4], distort[4], colour[4];

        for (int i = 0; i < 4; ++i) {
            OsLens L;
            CHECK(os_lens_build(&L, OS_LENS_ZOOM_RETRO, F[i], 5.6,
                                why, sizeof why));
            /* The separation is SOLVED against the paraxial trace, so landing
             * on the requested focal length is the check that the solve
             * converged -- not a tautology, because the thin-lens identity it
             * started from is 14 % out at the long end. */
            CHECK_NEAR(L.efl_mm, F[i], 1e-6);
            CHECK(os_lens_focus(&L, 3.0));

            track[i]   = L.total_track_mm;
            distort[i] = os_lens_distortion_pct(&L, 21.63);
            colour[i]  = 100.0 * (os_lens_efl_at(&L, OS_LINE_F)
                                - os_lens_efl_at(&L, OS_LINE_C)) / L.efl_mm;
            NOTE("zoom at %5.1f mm: track %6.2f mm, distortion %+7.2f %%, "
                 "colour %+6.3f %%", F[i], track[i], distort[i], colour[i]);
        }

        /* ---- 1. the glass actually MOVED ----
         * A scaled design's track is proportional to its focal length. This
         * one's runs the other way: the groups separate as it goes wide, so
         * the short setting is the LONG lens. Nothing that merely rescales can
         * do that. */
        CHECK(track[0] > track[3]);
        NOTE("the 45 mm setting is %.2fx longer than the 100 mm one",
             track[0] / track[3]);
        CHECK(track[0] / track[3] > 1.5);

        /* ---- 2. the distortion sweeps, monotonically ----
         * Barrel throughout, and growing hard toward the wide end because the
         * stop sits further behind the negative front group at every step. */
        for (int i = 0; i < 4; ++i) CHECK(distort[i] < 0.0);
        for (int i = 1; i < 4; ++i) CHECK(distort[i] > distort[i - 1]);
        NOTE("distortion sweeps %.1fx across the range",
             distort[0] / distort[3]);
        CHECK(distort[0] / distort[3] > 4.0);

        /* ---- 3. but the COLOUR does not move, and that is the good news ----
         *
         * I expected the residual to sweep with the separation and it does
         * not: -0.704 % at the wide end against -0.692 % at the long one, a
         * fiftieth of the distortion's swing. Each group is achromatic on its
         * own, so what is left is their own secondary spectrum, and that
         * travels with the glass rather than with the gap. A zoom that held
         * its geometry and lost its colour correction would be a bad zoom;
         * this one is the other way round.
         *
         * The level is another matter: -0.70 % against the ACHROMAT's
         * -0.058 %, twelve times worse, because the two groups carry far more
         * power than their sum and each one's residual is proportional to its
         * own. That is the price of building a zoom out of two doublets, and
         * it is a measurement rather than an apology. */
        CHECK(fabs(colour[0] - colour[3]) < 0.05);
        for (int i = 0; i < 4; ++i) CHECK(fabs(colour[i]) > 0.3);

        /* ---- 4. the contrast, measured against a design that scales ----
         * The achromat at the same two focal lengths keeps its character to
         * the last bit, because scaling preserves every angle. The zoom does
         * not. That is the whole difference between resizing a lens and
         * rebuilding one. */
        OsLens a45, a100;
        CHECK(os_lens_build(&a45,  OS_LENS_ACHROMAT_100, 45.0,  5.6, why, sizeof why));
        CHECK(os_lens_build(&a100, OS_LENS_ACHROMAT_100, 100.0, 5.6, why, sizeof why));
        CHECK(os_lens_focus(&a45, 3.0));
        CHECK(os_lens_focus(&a100, 3.0));
        ls_real c45  = 100.0 * (os_lens_efl_at(&a45,  OS_LINE_F)
                              - os_lens_efl_at(&a45,  OS_LINE_C)) / a45.efl_mm;
        ls_real c100 = 100.0 * (os_lens_efl_at(&a100, OS_LINE_F)
                              - os_lens_efl_at(&a100, OS_LINE_C)) / a100.efl_mm;
        CHECK_NEAR(c45, c100, 1e-12);            /* scaled: identical         */
        /* The zoom's DISTORTION is what moves; its colour is held. */
        CHECK(fabs(distort[0] - distort[3]) > 1.0);
        /* And the track scales exactly, which is the same statement about
         * lengths that the colour check makes about angles. */
        CHECK_NEAR(a45.total_track_mm / a100.total_track_mm, 0.45, 1e-9);

        /* ---- and it refuses what its mechanism cannot reach ----
         * Both ends, for different reasons: the long end is the groups
         * colliding, the wide end is the front element ceasing to cover the
         * frame. A lens that quietly returned something plausible outside its
         * range would be worse than one that will not build. */
        OsLens bad;
        CHECK(!os_lens_build(&bad, OS_LENS_ZOOM_RETRO, 200.0, 5.6, why, sizeof why));
        NOTE("refused at 200 mm: %s", why);
        CHECK(!os_lens_build(&bad, OS_LENS_ZOOM_RETRO, 24.0, 5.6, why, sizeof why));

        /* The range is reported, so a UI can stop at it rather than walking a
         * control past a build that then fails. */
        ls_real fmin = 0.0, fmax = 0.0;
        OsLens z;
        CHECK(os_lens_build(&z, OS_LENS_ZOOM_RETRO, 60.0, 5.6, why, sizeof why));
        CHECK(os_lens_focal_range_mm(&z, &fmin, &fmax));
        CHECK_NEAR(fmin, 45.0, 1e-12);
        CHECK_NEAR(fmax, 100.0, 1e-12);
        /* And the same answer before anything is built, which is what the
         * viewer needs when the design is switched. */
        ls_real dmin = 0.0, dmax = 0.0;
        CHECK(os_lens_design_focal_range(OS_LENS_ZOOM_RETRO, &dmin, &dmax));
        CHECK_NEAR(dmin, fmin, 1e-12);
        CHECK_NEAR(dmax, fmax, 1e-12);
        /* A design that scales freely reports NO range, so a caller can tell
         * "unbounded" from "bounded here". */
        CHECK(!os_lens_design_focal_range(OS_LENS_ACHROMAT_100, &dmin, &dmax));
        CHECK(!os_lens_focal_range_mm(&a100, &dmin, &dmax));
    }

    SECTION("lens: distortion, the aberration that blurs nothing");
    {
        /* Distortion moves an image point instead of spreading it, which is
         * why it needs a measurement of its own: it cannot be seen in a spot
         * diagram, and on a field of round objects it is invisible, because a
         * blob moved slightly outward is still a blob.
         *
         * Measured from the CHIEF ray -- the one through the centre of the
         * entrance pupil -- against the paraxial image height at the conjugate
         * the lens is focused at. */
        char why[256];
        static const OsPrescriptionId ID[3] = {
            OS_LENS_THIN, OS_LENS_SINGLET_100, OS_LENS_ACHROMAT_100 };
        /* Measured, at the image height a 0.75 rad object reaches. */
        static const double WANT[3] = { -2.106, -2.411, -0.429 };

        for (int k = 0; k < 3; ++k) {
            OsLens L;
            CHECK(os_lens_build(&L, ID[k], 100.0, 5.0, why, sizeof why));
            L.blades = 0;
            CHECK(os_lens_set_fnumber(&L, 5.0));
            CHECK(os_lens_focus(&L, 2.0));

            /* Where a 0.75 rad object lands, paraxially. */
            ls_real s   = 2000.0 - (L.ffd_mm + L.efl_mm);
            ls_real sp  = 1.0 / (1.0 / L.efl_mm - 1.0 / s);
            ls_real h   = tan(0.75) * 2000.0 * (sp / s);
            ls_real got = os_lens_distortion_pct(&L, h);

            NOTE("%-11s %+7.3f %% at h = %.2f mm",
                 os_prescription_name(ID[k]), got, h);
            CHECK_NEAR(got, WANT[k], 2e-3);
            /* Every design here is BARREL. A sign slip would read as
             * pincushion and look just as plausible on a number. */
            CHECK(got < 0.0);
        }

        /* ---- it is zero on axis and grows with height ----
         *
         * Not merely nonzero somewhere: distortion is a field aberration, so
         * it has to vanish on the axis and increase outward. A constant offset
         * would be a magnification error, which is a different mistake. */
        OsLens L;
        CHECK(os_lens_build(&L, OS_LENS_ACHROMAT_100, 100.0, 5.0, why, sizeof why));
        L.blades = 0;
        CHECK(os_lens_set_fnumber(&L, 5.0));
        CHECK(os_lens_focus(&L, 2.0));

        CHECK(os_lens_distortion_pct(&L, 0.0) == 0.0);   /* nowhere to move */
        CHECK(os_lens_distortion_pct(&L, -5.0) == 0.0);

        ls_real prev = 0.0;
        for (ls_real h = 25.0; h <= 100.0; h += 25.0) {
            ls_real d = fabs(os_lens_distortion_pct(&L, h));
            CHECK(d >= prev - 1e-9);
            prev = d;
        }
        CHECK(prev > 0.3);          /* and it really does get somewhere */

        /* ---- it blurs NOTHING ----
         *
         * The claim that makes it a separate row from SPOT. At the focused
         * distance the defocus blur is exactly zero, and the distortion is
         * not -- so the two cannot be measurements of the same thing, and no
         * amount of refocusing turns one into the other. */
        ls_real hh = 90.0;
        CHECK_NEAR(os_lens_coc_mm(&L, 2.0), 0.0, 1e-9);
        CHECK(fabs(os_lens_distortion_pct(&L, hh)) > 0.1);

        /* ---- and it is measured at the CONJUGATE, which is the trap ----
         *
         * f*tan(theta) is the paraxial image height for an object at INFINITY.
         * Using it on a lens focused at 2 m is the mistake this function
         * exists to make impossible, and it is not a small one: on this design
         * it reports several per cent of PINCUSHION where there is half a per
         * cent of barrel -- right magnitude, wrong sign, entirely convincing.
         *
         * So: the honest number, and the number the wrong reference gives,
         * measured side by side at the same field. */
        ls_real ss  = 2000.0 - (L.ffd_mm + L.efl_mm);
        ls_real spp = 1.0 / (1.0 / L.efl_mm - 1.0 / ss);
        ls_real mag = spp / ss;
        ls_real theta = 0.75;
        ls_real h_par = tan(theta) * 2000.0 * mag;    /* correct reference   */
        ls_real h_inf = L.efl_mm * tan(theta);        /* infinity's reference */
        NOTE("at 0.75 rad the paraxial height is %.2f mm focused at 2 m, but "
             "%.2f mm at infinity -- a %.0f%% different yardstick",
             h_par, h_inf, 100.0 * fabs(h_inf / h_par - 1.0));
        CHECK(fabs(h_inf / h_par - 1.0) > 0.02);      /* they really differ  */

        /* Focused at infinity the answer converges rather than jumping: the
         * conjugate is a continuum and so is the distortion on it. */
        ls_real at_2m = os_lens_distortion_pct(&L, 21.63);
        CHECK(os_lens_focus(&L, 1000.0));
        ls_real at_1km = os_lens_distortion_pct(&L, 21.63);
        CHECK(os_lens_focus(&L, HUGE_VAL));
        ls_real at_inf = os_lens_distortion_pct(&L, 21.63);
        NOTE("the same 21.63 mm corner: %+.3f %% at 2 m, %+.3f %% at 1 km, "
             "%+.3f %% at infinity", at_2m, at_1km, at_inf);
        CHECK(isfinite(at_inf));
        /* 1 km is nearly infinity, and must read nearly the same. An earlier
         * version faked infinity with a 1e9 mm object and lost fourteen digits
         * to cancellation in the sphere intersection; it reported +3 % here,
         * off by a hundred times and by a sign. */
        CHECK(fabs(at_1km - at_inf) < 0.01);
    }

    SECTION("lens: the entrance pupil of a stop behind the front element");
    {
        /* THE branch nothing shipped reaches, and it was wrong.
         *
         * Every prescription here puts the stop on surface 0, so
         * entrance_pupil short-circuits -- the stop IS the entrance pupil, and
         * the backward y-nu walk that images it through the glass in front is
         * never called. It negated the radius on top of a transfer that was
         * already in the unmirrored frame, which flipped the sign of the
         * curvature term, and no test could see it. A double Gauss would have
         * shipped with its f-number wrong by ten per cent.
         *
         * So: move the ideal design's stop to its SECOND surface and ask for
         * the pupil. That images a plane 20 mm inside n = 2 back out through
         * the R = +100 entry surface into air, which the single-surface
         * conjugate equation answers in closed form:
         *
         *     n'/s' - n/s = (n' - n)/R      with the ray going -z, so mirror:
         *     1/s' - 2/(-20) = (1 - 2)/(-100)  =>  s' = -100/9
         *     m = (n s')/(n' s) = (2 * -100/9)/(1 * -20) = 10/9
         *
         * The image lands 100/9 mm on the far side of the vertex from the
         * stop -- a VIRTUAL pupil, in front of the glass, which is the usual
         * arrangement and exactly the case a sign error survives in, because
         * the wrong answer is also a plausible-looking pupil. */
        OsLens L;
        CHECK(os_lens_build(&L, OS_LENS_THIN, 0.0, 0.0, why, sizeof why));
        L.stop_index = 1;
        CHECK(os_lens_set_fnumber(&L, 4.0));

        NOTE("stop on surface 1: pupil at z = %.6f mm, magnification %.6f",
             L.ep_z_mm, L.ep_mag);
        CHECK_NEAR(L.ep_z_mm, 100.0 / 9.0, 1e-9);
        CHECK_NEAR(L.ep_mag, 10.0 / 9.0, 1e-9);

        /* And the pupil's SIZE follows the magnification, which is the number
         * the f-number is computed from -- getting the sign wrong here does
         * not produce an error, it produces a lens that passes a different
         * amount of light than it claims. */
        CHECK_NEAR(L.ep_semi_ap_mm, L.stop_semi_ap_mm * 10.0 / 9.0, 1e-9);
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
