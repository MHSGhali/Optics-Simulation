/* test_render_focus.c — does the picture agree with the panel?
 *
 * WHAT THIS SUITE EXISTS FOR
 *   The depth-of-field numbers on screen and the blur in the rendered image are
 *   two entirely independent computations. The panel comes from
 *   os_lens_coc_mm(), a paraxial model of DEFOCUS. The image comes from real
 *   rays through real glass. Nothing compared them, and they disagreed:
 *   focused at 4.57 m the panel called the 5 m target sharp, and the render
 *   made it the blurriest thing in frame.
 *
 *   Neither was wrong. The 5 m target sits 13.7 mm off axis, and an
 *   uncorrected doublet's coma and astigmatism there dwarf the defocus the
 *   panel was measuring. They were answers to different questions, and only
 *   one of them was the question being asked.
 *
 *   So these tests tie the two together, and pin the direction of the effect
 *   so nobody "fixes" the honest one back into the convenient one.
 */
#include "test.h"
#include "tests.h"

#include "opticsim/scenedesc.h"
#include "opticsim/camera.h"
#include "opticsim/render.h"

#include <math.h>
#include <string.h>

void os_test_render_focus(void) {
    char why[256];

    SECTION("focus: on axis, the sharpest subject is the one focused on");
    {
        /* ON AXIS the paraxial model IS the whole story, so this is the case
         * where the two computations must agree exactly -- and sweeping the
         * focus means it has to hold for every target, not one lucky one. */
        OsLens L;
        CHECK(os_lens_build(&L, OS_LENS_ACHROMAT_100, 100.0, 5.0, why, sizeof why));
        L.blades = 0;
        CHECK(os_lens_set_fnumber(&L, 5.0));

        static const double D[] = { 1.0, 1.5, 2.0, 3.0, 5.0 };
        for (int k = 0; k < 5; ++k) {
            CHECK(os_lens_focus(&L, D[k]));
            int best = -1;
            ls_real best_spot = HUGE_VAL;
            for (int i = 0; i < 5; ++i) {
                ls_real sp = os_lens_spot_mm(&L, D[i], 0.0, 13);
                if (sp < best_spot) { best_spot = sp; best = i; }
            }
            CHECK(best == k);
        }
    }

    SECTION("focus: the traced spot tracks the model on BOTH sides");
    {
        /* The gap that let the disagreement through. The only test relating
         * the trace to the model looked at an object FARTHER than focus; a
         * sign slip on the near side would have passed it while producing
         * exactly the reported symptom. */
        OsLens L;
        CHECK(os_lens_build(&L, OS_LENS_ACHROMAT_100, 100.0, 8.0, why, sizeof why));
        L.blades = 0;
        CHECK(os_lens_set_fnumber(&L, 8.0));
        CHECK(os_lens_focus(&L, 3.0));

        /* Near and far of a 3 m focus, on axis where defocus is all there is. */
        static const double OBJ[] = { 1.5, 2.0, 2.5, 3.5, 5.0, 9.0 };
        for (int i = 0; i < 6; ++i) {
            ls_real model  = os_lens_coc_mm(&L, OBJ[i]);
            ls_real traced = os_lens_spot_mm(&L, OBJ[i], 0.0, 21);
            /* An RMS diameter is smaller than the geometric full width -- for
             * a uniform disc by 1/sqrt(2) -- so the two are related, not
             * equal. The claim is that they track: same order, same trend,
             * and both grow away from focus on either side. */
            CHECK(traced > 0.0);
            CHECK(traced < model * 1.2);
            CHECK(traced > model * 0.4);
        }

        /* Blur grows monotonically as an object leaves focus, in BOTH
         * directions. This is what a dropped fabs() would break on one side
         * only. */
        ls_real prev = os_lens_spot_mm(&L, 3.0, 0.0, 21);
        for (double d = 3.2; d <= 9.0; d += 0.6) {
            ls_real sp = os_lens_spot_mm(&L, d, 0.0, 21);
            CHECK(sp >= prev - 1e-9);
            prev = sp;
        }
        prev = os_lens_spot_mm(&L, 3.0, 0.0, 21);
        for (double d = 2.8; d >= 1.2; d -= 0.2) {
            ls_real sp = os_lens_spot_mm(&L, d, 0.0, 21);
            CHECK(sp >= prev - 1e-9);
            prev = sp;
        }
    }

    SECTION("focus: off axis, field aberration can beat defocus outright");
    {
        /* THE reported case, pinned as a fact rather than left as a surprise.
         * Focused at 4.57 m: the 5 m target is dead centre of the paraxial
         * depth of field and the 3 m target is well outside it, yet at their
         * real field positions the 3 m one is several times the sharper. */
        OsLens L;
        CHECK(os_lens_build(&L, OS_LENS_ACHROMAT_100, 100.0, 5.0, why, sizeof why));
        L.blades = 0;
        CHECK(os_lens_set_fnumber(&L, 5.0));
        CHECK(os_lens_focus(&L, 4.57));

        /* The rail's own layout: offset is 0.140 * distance for the 5 m target
         * and 0.070 * distance for the 3 m one. */
        ls_real spot3 = os_lens_spot_mm(&L, 3.0, 0.070 * 3.0, 15);
        ls_real spot5 = os_lens_spot_mm(&L, 5.0, 0.140 * 5.0, 15);
        ls_real par3  = os_lens_coc_mm(&L, 3.0);
        ls_real par5  = os_lens_coc_mm(&L, 5.0);

        NOTE("at 4.57 m focus -- 3M: paraxial %.4f, real %.4f mm", par3, spot3);
        NOTE("                  5M: paraxial %.4f, real %.4f mm", par5, spot5);

        /* Defocus alone says 5 m wins by a wide margin... */
        CHECK(par5 < par3 * 0.25);
        /* ...and the real lens says the opposite, by a wide margin. */
        CHECK(spot3 < spot5 * 0.5);

        /* Move that same 5 m subject ONTO the axis and it becomes the sharpest
         * thing in the scene, which is what identifies the cause as field
         * rather than distance. */
        ls_real spot5_axis = os_lens_spot_mm(&L, 5.0, 0.0, 15);
        NOTE("the same 5M subject on axis: %.4f mm", spot5_axis);
        CHECK(spot5_axis < spot5 * 0.2);
        CHECK(spot5_axis < spot3);
    }

    SECTION("focus: the ring answers the question the row cannot");
    {
        /* THE reason there are two depth rails.
         *
         * The section above pins the row's failure: spread targets sideways to
         * stop them stacking in depth and you have put them at five different
         * field angles, where coma and astigmatism swamp the defocus you meant
         * to measure. The ring is the same five targets at ONE angular radius,
         * so the field aberration is identical for all of them and cancels out
         * of every comparison -- leaving focus as the only variable, which is
         * what the scene has always claimed to be about.
         *
         * Asked the plainest possible way: focus on each target in turn, and
         * see whether it comes out the sharpest thing in the frame. That is
         * the promise a focus control makes, and it is the promise the row
         * breaks. Both scenes are read from the presets themselves, so this
         * cannot drift from what the program actually ships. */
        static const char *const STAGE[2] = { "row", "ring" };
        static const OsStageId ID[2] = { OS_STAGE_DEPTH_RAIL,
                                         OS_STAGE_DEPTH_RING };
        int hits[2] = { 0, 0 };
        ls_real worst_miss[2] = { 1.0, 1.0 };

        for (int k = 0; k < 2; ++k) {
            OsSceneDesc d;
            os_scenedesc_preset(&d, ID[k]);

            OsLens L;
            CHECK(os_lens_build(&L, OS_LENS_ACHROMAT_100, 100.0, 5.0,
                                why, sizeof why));
            L.blades = 0;
            CHECK(os_lens_set_fnumber(&L, 5.0));

            for (int f = 0; f < 5; ++f) {
                CHECK(os_lens_focus(&L, -d.obj[f].centre.z));

                int best = -1;
                ls_real best_spot = HUGE_VAL, focused_spot = HUGE_VAL;
                for (int i = 0; i < 5; ++i) {
                    ls_real z = -d.obj[i].centre.z;
                    /* Each target's REAL field height, from where it is. */
                    ls_real h = sqrt(d.obj[i].centre.x * d.obj[i].centre.x
                                   + d.obj[i].centre.y * d.obj[i].centre.y);
                    ls_real sp = os_lens_spot_mm(&L, z, h, 15);
                    if (i == f) focused_spot = sp;
                    if (sp < best_spot) { best_spot = sp; best = i; }
                }
                if (best == f) hits[k]++;
                ls_real ratio = best_spot / focused_spot;
                if (ratio < worst_miss[k]) worst_miss[k] = ratio;
            }
            NOTE("%-4s: %d of 5 focus settings pick their own target; at worst "
                 "the sharpest is %.1fx tighter than the focused one",
                 STAGE[k], hits[k], 1.0 / worst_miss[k]);
        }

        /* The row gets it wrong somewhere -- if it ever stops doing so, the
         * section above is describing a scene that no longer exists and both
         * should be revisited together. */
        CHECK(hits[0] < 5);
        /* And the ring gets it right everywhere, which is the whole claim. */
        CHECK(hits[1] == 5);
        /* By a wide margin, not by a hair: the row's worst miss is several
         * times over, the ring has none at all. */
        CHECK(worst_miss[0] < 0.5);
        CHECK_NEAR(worst_miss[1], 1.0, 1e-12);
    }

    SECTION("focus: the spot grows with field, and stops when nothing gets through");
    {
        OsLens L;
        CHECK(os_lens_build(&L, OS_LENS_ACHROMAT_100, 100.0, 5.0, why, sizeof why));
        L.blades = 0;
        CHECK(os_lens_set_fnumber(&L, 5.0));
        CHECK(os_lens_focus(&L, 5.0));

        /* At the focused distance, defocus is zero everywhere -- so every
         * millimetre of spot is field aberration, and it can only grow. */
        ls_real prev = 0.0;
        for (double hgt = 0.0; hgt <= 0.75; hgt += 0.125) {
            ls_real sp = os_lens_spot_mm(&L, 5.0, hgt, 13);
            if (!isfinite(sp)) break;             /* past what the lens covers */
            CHECK(sp >= prev - 1e-9);
            prev = sp;
        }
        CHECK(prev > 0.05);       /* it really does degrade, and measurably */

        /* Far off axis the spot does not become infinite -- rays still get
         * through. A bare prescription has no barrel and no field stop, so at
         * 80 degrees off axis a few rays still find a path through the glass
         * and land hundreds of millimetres from the axis. What limits the
         * field is the SENSOR, not the lens, and saying so here stops someone
         * "fixing" the honest answer into a tidy infinity. */
        ls_real wild = os_lens_spot_mm(&L, 5.0, 50.0, 13);
        NOTE("50 m off axis at 5 m: spot %.1f mm -- no field stop in a bare "
             "prescription", wild);
        CHECK(wild > 100.0);
        /* And nonsense in is refused rather than producing a number. */
        CHECK(!isfinite(os_lens_spot_mm(&L, -1.0, 0.0, 13)));
        CHECK(!isfinite(os_lens_spot_mm(&L, 0.0, 0.0, 13)));
    }

    SECTION("focus: a render agrees with the traced spot");
    {
        /* End to end, through the ordinary os_render_pass path, so the model
         * and the picture are tied together at least once rather than only via
         * a direct trace.
         *
         * A subject at the focused distance and on axis must render with a
         * harder edge than the same subject moved well out of focus. Measured
         * on the silhouette against the background -- not across the shaded
         * interior, whose terminator is a LIGHTING feature and says nothing
         * about focus. */
        const int W = 220, H = 150;
        ls_real width[2];

        for (int k = 0; k < 2; ++k) {
            OsSceneDesc d;
            memset(&d, 0, sizeof d);
            d.cam_eye = v3(0, 0, 0);
            d.cam_target = v3(0, 0, -1);

            int oi = os_scenedesc_add_object(&d, OS_OBJ_SPHERE);
            d.obj[oi].centre = v3(0.0, 0.0, -2.0);
            d.obj[oi].radius = 0.10;
            d.obj[oi].rgb[0] = d.obj[oi].rgb[1] = d.obj[oi].rgb[2] = 0.9;

            /* A SPHERE light, not a rect: a rect's normal is fixed pointing
             * down, so one placed "head on" between camera and subject lights
             * nothing at all. A sphere radiates every way and puts an even
             * front light on the subject, which is what a silhouette
             * measurement needs -- a terminator across the equator would be
             * measuring the lighting instead of the focus. */
            int li = os_scenedesc_add_light(&d, OS_LIGHT_SPHERE);
            /* OUT of shot, and to the same side as the edge being measured. A
             * lamp in front of the subject is itself a huge defocused disc
             * across the whole frame; at 1 m the frame is only 0.1 m wide, so
             * 0.6 m off axis puts it well outside. */
            d.lit[li].centre = v3(0.6, 0.4, -1.0);
            d.lit[li].radius = 0.05;
            d.lit[li].flux_lm = 200000.0;

            OsStage st;
            CHECK(os_scenedesc_build(&d, &st));

            OsCamera cam;
            CHECK(os_camera_build(&cam, OS_LENS_ACHROMAT_100, 100.0, 5.0,
                                  20.0, W, H, why, sizeof why));
            /* Focused ON the subject, then far off it. */
            CHECK(os_lens_focus(&cam.lens, k == 0 ? 2.0 : 6.0));
            os_camera_refresh(&cam);
            os_camera_look_at(&cam, st.cam_eye, st.cam_target, v3(0, 1, 0));

            Film film;
            CHECK(ls_film_init(&film, W, H));
            OsRenderOpts opt = { 64, 3, 0, 0x853C49E6748FEA9Bull };
            for (int p = 0; p < 8; ++p)
                os_render_pass(&film, &cam, &st, &opt, p);

            /* Measure the width of the GRADIENT PEAK at the silhouette.
             *
             * Not a 10-90 % level crossing across the disc: a lit sphere dims
             * gradually toward its limb, so that measure spans most of the
             * radius and reports the same number however the lens is focused.
             * Blur convolves the edge, which widens the gradient peak there
             * and leaves the slow limb falloff as a low broad ramp -- so the
             * peak's half-width is the blur and the shading is not. */
            int cy = H / 2;
            ls_real row[512];
            for (int x = 0; x < W && x < 512; ++x) {
                Spectrum sp = ls_film_mean(&film, x, cy);
                row[x] = ls_spectrum_integrate(&sp);
            }
            ls_real g[512];
            g[0] = g[W - 1] = 0.0;
            for (int x = 1; x < W - 1; ++x)
                g[x] = fabs(row[x + 1] - row[x - 1]) * 0.5;

            /* The outer edge is the strongest gradient on the lit side. */
            int pk = W / 2;
            for (int x = W / 2; x < W - 1; ++x) if (g[x] > g[pk]) pk = x;

            /* Width = step height / peak slope.
             *
             * Sub-pixel and continuous, where counting how many samples exceed
             * a threshold is neither: at this size a real four-pixel
             * difference lands inside the quantisation of a discrete count and
             * the answer comes back as noise. Both renders share geometry,
             * lighting and seed, so the step is the same in each and this is a
             * clean comparison of slope. */
            ls_real bg = row[W - 3];
            ls_real peak = 0.0;
            for (int x = 0; x < W; ++x) if (row[x] > peak) peak = row[x];
            ls_real step = peak - bg;
            width[k] = (g[pk] > 0.0) ? step / g[pk] : -1.0;

            ls_film_free(&film);
            os_camera_free(&cam);
            os_stage_free(&st);
        }

        NOTE("rendered silhouette width (step/slope): focused %.1f px, defocused %.1f px",
             width[0], width[1]);
        CHECK(width[0] > 0.0);
        CHECK(width[1] > 0.0);
        /* The render must show the focused one as the harder edge. If this
         * ever fails, the picture and the model have parted company again. */
        CHECK(width[1] > width[0]);
    }
}
