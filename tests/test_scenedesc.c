/* test_scenedesc.c — the editable scene.
 *
 * Two claims dominate this file. First, an id never changes meaning: deleting
 * an object must not silently repoint the panel at its neighbour. Second, no
 * value reachable from the UI can abort a build -- ls_light_finalize ASSERTS
 * that a light's geometry and its flux agree and that its spectrum normalises,
 * and both are reachable from a control that was merely dragged too far.
 */
#include "test.h"
#include "tests.h"

#include "opticsim/scenedesc.h"
#include "opticsim/camera.h"
#include "opticsim/render.h"
#include "lightsim/units.h"

#include <math.h>
#include <string.h>

void os_test_scenedesc(void) {
    SECTION("scenedesc: an id never changes meaning");
    {
        /* THE invariant. Compacting on delete would renumber everything after
         * the hole, and the selection is an id -- so deleting one sphere would
         * leave the panel editing a different one with nothing on screen to
         * say so. */
        OsSceneDesc d;
        memset(&d, 0, sizeof d);

        int id[5];
        for (int i = 0; i < 5; ++i) {
            id[i] = os_scenedesc_add_object(&d, OS_OBJ_SPHERE);
            CHECK(id[i] == i);
            d.obj[id[i]].centre = v3(0.0, 0.0, -(ls_real)(i + 1));
        }
        CHECK(os_scenedesc_count_objects(&d) == 5);

        CHECK(os_scenedesc_delete_object(&d, id[2]));
        CHECK(os_scenedesc_count_objects(&d) == 4);

        /* Every survivor still names exactly what it named before. */
        for (int i = 0; i < 5; ++i) {
            if (i == 2) { CHECK(!d.obj[id[i]].alive); continue; }
            CHECK(d.obj[id[i]].alive);
            CHECK_NEAR(d.obj[id[i]].centre.z, -(ls_real)(i + 1), 1e-12);
        }

        /* The high-water mark does NOT shrink -- that is what reserves the id. */
        CHECK(d.nobj == 5);

        /* A second delete of the same id changes nothing and says so, rather
         * than corrupting a count. */
        CHECK(!os_scenedesc_delete_object(&d, id[2]));
        CHECK(!os_scenedesc_delete_object(&d, -1));
        CHECK(!os_scenedesc_delete_object(&d, 99));

        /* Stepping the selection skips the tombstone without the caller
         * needing to know it is there. */
        CHECK(os_scenedesc_next_object(&d, 0) == 0);
        CHECK(os_scenedesc_next_object(&d, 2) == 3);
        CHECK(os_scenedesc_next_object(&d, 5) == -1);
    }

    SECTION("scenedesc: capacity is refused, not wrapped");
    {
        OsSceneDesc d;
        memset(&d, 0, sizeof d);
        for (int i = 0; i < OS_MAX_OBJECTS; ++i)
            CHECK(os_scenedesc_add_object(&d, OS_OBJ_SPHERE) == i);
        CHECK(os_scenedesc_add_object(&d, OS_OBJ_SPHERE) == -1);
        CHECK(d.nobj == OS_MAX_OBJECTS);          /* unchanged by the refusal */

        for (int i = 0; i < OS_MAX_LIGHTS; ++i)
            CHECK(os_scenedesc_add_light(&d, OS_LIGHT_SPHERE) == i);
        CHECK(os_scenedesc_add_light(&d, OS_LIGHT_SPHERE) == -1);
        CHECK(d.nlit == OS_MAX_LIGHTS);

        /* A full description still builds -- the flat scene has room for one
         * prim per object AND one per light's emissive face. */
        OsStage st;
        CHECK(os_scenedesc_build(&d, &st));
        CHECK(st.scene.nprims == OS_MAX_OBJECTS + OS_MAX_LIGHTS);
        CHECK(st.scene.nlights == OS_MAX_LIGHTS);
        os_stage_free(&st);
    }

    SECTION("scenedesc: no reachable value can abort a build");
    {
        /* ls_light_finalize asserts that flux and geometry agree and that the
         * spectrum normalises. A zero-size light divides by zero; a colour
         * temperature below the visible underflows every bin. Both are one
         * over-enthusiastic drag away, and both abort inside a build rather
         * than at the control that caused them -- so every extreme is driven
         * here, and the build has to survive all of them. */
        static const double WILD[] = {
            0.0, -1.0, -1e30, 1e30, 1e-300, 1e300, 0.5, 3.0
        };
        const int NW = (int)(sizeof WILD / sizeof WILD[0]);

        for (int w = 0; w < NW; ++w) {
            for (int k = 0; k < OS_LIGHT_KIND_COUNT; ++k) {
                OsSceneDesc d;
                memset(&d, 0, sizeof d);
                int li = os_scenedesc_add_light(&d, (OsLightKind)k);
                CHECK(li == 0);
                OsLight *l = &d.lit[li];

                /* Every field at once, so no ordering hides a case. */
                l->centre  = v3(WILD[w], WILD[w], WILD[w]);
                l->radius  = WILD[w];
                l->size_u  = WILD[w];
                l->size_v  = WILD[w];
                l->flux_lm = WILD[w];
                l->cct_k   = WILD[w];
                os_scenedesc_clamp_light(l);

                /* Clamped, therefore legal, therefore safe to finalize. */
                CHECK(l->radius >= OS_SIZE_MIN_M);
                CHECK(l->size_u >= OS_SIZE_MIN_M);
                CHECK(l->size_v >= OS_SIZE_MIN_M);
                CHECK(l->cct_k  >= OS_CCT_MIN_K);
                CHECK(l->cct_k  <= OS_CCT_MAX_K);
                CHECK(isfinite(l->flux_lm));
                CHECK(isfinite(l->centre.x));

                OsStage st;
                CHECK(os_scenedesc_build(&d, &st));   /* must not abort */
                CHECK(st.scene.nlights == 1);
                os_stage_free(&st);
            }
        }

        /* NaN too: it fails BOTH comparisons of a three-way clamp, so the
         * obvious implementation passes it straight through. */
        OsSceneDesc d;
        memset(&d, 0, sizeof d);
        int li = os_scenedesc_add_light(&d, OS_LIGHT_RECT);
        d.lit[li].cct_k   = nan("");
        d.lit[li].size_u  = nan("");
        d.lit[li].flux_lm = nan("");
        d.lit[li].centre  = v3(nan(""), nan(""), nan(""));
        os_scenedesc_clamp_light(&d.lit[li]);
        CHECK(isfinite(d.lit[li].cct_k));
        CHECK(isfinite(d.lit[li].size_u));
        CHECK(isfinite(d.lit[li].flux_lm));
        CHECK(isfinite(d.lit[li].centre.x));

        int oi = os_scenedesc_add_object(&d, OS_OBJ_PLANE);
        d.obj[oi].normal = v3(0.0, 0.0, 0.0);      /* normalises to NaN */
        d.obj[oi].radius = nan("");
        d.obj[oi].rgb[0] = nan("");
        os_scenedesc_clamp_object(&d.obj[oi]);
        CHECK(isfinite(d.obj[oi].normal.z));
        CHECK_NEAR(v3len(d.obj[oi].normal), 1.0, 1e-12);
        CHECK(isfinite(d.obj[oi].radius));
        CHECK(isfinite(d.obj[oi].rgb[0]));

        OsStage st;
        CHECK(os_scenedesc_build(&d, &st));
        os_stage_free(&st);
    }

    SECTION("scenedesc: changing kind twice leaves no stale quantity");
    {
        /* Re-homing gated on "only if unset" re-homes the FIRST time and never
         * again, so a value from the wrong quantity survives the second pass.
         * Two full laps is the smallest test that can see it. */
        OsSceneDesc d;
        memset(&d, 0, sizeof d);
        int oi = os_scenedesc_add_object(&d, OS_OBJ_SPHERE);
        int li = os_scenedesc_add_light(&d, OS_LIGHT_SPHERE);

        for (int lap = 0; lap < 2; ++lap) {
            for (int k = 0; k < OS_OBJ_KIND_COUNT; ++k) {
                d.obj[oi].kind = (OsObjKind)k;
                os_scenedesc_rehome_object(&d.obj[oi]);
                CHECK(d.obj[oi].radius >= OS_SIZE_MIN_M);
                CHECK(d.obj[oi].radius <= OS_SIZE_MAX_M);
                CHECK_NEAR(v3len(d.obj[oi].normal), 1.0, 1e-12);
            }
            for (int k = 0; k < OS_LIGHT_KIND_COUNT; ++k) {
                d.lit[li].kind = (OsLightKind)k;
                os_scenedesc_rehome_light(&d.lit[li]);
                CHECK(d.lit[li].radius >= OS_SIZE_MIN_M);
                CHECK(d.lit[li].size_u >= OS_SIZE_MIN_M);
                CHECK(d.lit[li].size_v >= OS_SIZE_MIN_M);
                /* And every configuration still builds. */
                OsStage st;
                CHECK(os_scenedesc_build(&d, &st));
                os_stage_free(&st);
            }
        }
    }

    SECTION("scenedesc: lumens are what is authored, and they survive a colour change");
    {
        /* The point of authoring in lumens: a 4000 lm lamp is still a 4000 lm
         * lamp after being warmed up, even though the watts required change.
         * Storing watts instead would silently rebrighten it every time the
         * colour moved. */
        OsSceneDesc d;
        memset(&d, 0, sizeof d);
        int li = os_scenedesc_add_light(&d, OS_LIGHT_SPHERE);
        d.lit[li].flux_lm = 4000.0;

        ls_real watts[2];
        const double CCT[2] = { 2700.0, 6500.0 };
        for (int i = 0; i < 2; ++i) {
            d.lit[li].cct_k = CCT[i];
            OsStage st;
            CHECK(os_scenedesc_build(&d, &st));
            CHECK(st.scene.nlights == 1);
            const Light *L = &st.scene.lights[0];

            /* The authored number survives into the built light. */
            CHECK(L->flux_in_lumens);
            CHECK_NEAR(L->flux_authored, 4000.0, 1e-12);

            /* And the round trip closes: watts back to lumens is where we
             * started, which is the check that the one conversion seam in
             * units.h is actually reversible. */
            Spectrum full = ls_spectrum_scale(L->s_hat, L->phi_e);
            CHECK_NEAR(ls_photometric(&full), 4000.0, 1e-6);

            watts[i] = L->phi_e;
            os_stage_free(&st);
        }
        NOTE("4000 lm needs %.3f W at 2700 K and %.3f W at 6500 K",
             watts[0], watts[1]);
        /* Same lumens, different watts -- which is the whole reason lumens are
         * the authored quantity rather than a display conversion. */
        CHECK(fabs(watts[0] - watts[1]) > 1e-3);
    }

    SECTION("scenedesc: the emissive face and its light agree by construction");
    {
        /* The pairing that has exactly one authority. When it drifts, "the
         * render shows a light the NEE path no longer samples, or the reverse
         * -- and neither picture looks obviously wrong". The hard-coded bokeh
         * stage this replaced had exactly that: its emissive surface was set
         * independently of the light's flux and came out 83x too bright. */
        OsSceneDesc d;
        memset(&d, 0, sizeof d);
        os_scenedesc_add_light(&d, OS_LIGHT_SPHERE);
        os_scenedesc_add_light(&d, OS_LIGHT_RECT);

        OsStage st;
        CHECK(os_scenedesc_build(&d, &st));
        CHECK(st.scene.nlights == 2);

        int paired = 0;
        for (int i = 0; i < st.scene.nprims; ++i) {
            const Prim *p = &st.scene.prims[i];
            if (p->light_id < 0) continue;
            paired++;
            CHECK(p->light_id < st.scene.nlights);
            const Light *L = &st.scene.lights[p->light_id];
            const Material *m = &st.scene.mats[p->mat_id];
            CHECK(m->emissive);

            /* The face's radiance IS the light's radiance -- not a number set
             * beside it. */
            Spectrum want = ls_spectrum_scale(L->s_hat, L->radiance);
            for (int b = 0; b < LS_NBINS; ++b)
                CHECK_NEAR(m->le.v[b], want.v[b], 1e-6);

            /* And the geometry matches too. */
            CHECK_NEAR(p->c.x, L->p.x, 1e-12);
            CHECK_NEAR(p->c.z, L->p.z, 1e-12);
        }
        CHECK(paired == 2);        /* exactly one face per light, no more */
        os_stage_free(&st);
    }

    SECTION("scenedesc: doubling the flux doubles the light on the film");
    {
        /* The test that says the intensity control is wired to physics rather
         * than to a display gain. Measured on the film in physical units, at
         * the centre where there is no vignetting to muddy the ratio. */
        char why[256];
        const int W = 40, H = 27;
        ls_real energy[2];

        for (int k = 0; k < 2; ++k) {
            OsSceneDesc d;
            os_scenedesc_preset(&d, OS_STAGE_DEPTH_RAIL);
            for (int i = 0; i < d.nlit; ++i)
                if (d.lit[i].alive) d.lit[i].flux_lm *= (k == 0 ? 1.0 : 2.0);

            OsStage st;
            CHECK(os_scenedesc_build(&d, &st));

            OsCamera cam;
            CHECK(os_camera_build(&cam, OS_LENS_ACHROMAT_100, 100.0, 5.0,
                                  36.0, W, H, why, sizeof why));
            CHECK(os_lens_focus(&cam.lens, 2.0));
            os_camera_refresh(&cam);
            os_camera_look_at(&cam, st.cam_eye, st.cam_target, v3(0, 1, 0));

            Film film;
            CHECK(ls_film_init(&film, W, H));
            OsRenderOpts opt = { 64, 4, 1, 0x853C49E6748FEA9Bull };
            os_render_pass(&film, &cam, &st, &opt, 0);

            ls_real sum = 0.0;
            for (int y = H / 2 - 3; y <= H / 2 + 3; ++y)
                for (int x = W / 2 - 3; x <= W / 2 + 3; ++x) {
                    Spectrum s = ls_film_mean(&film, x, y);
                    sum += ls_spectrum_integrate(&s);
                }
            energy[k] = sum;

            ls_film_free(&film);
            os_camera_free(&cam);
            os_stage_free(&st);
        }
        ls_real ratio = energy[1] / energy[0];
        NOTE("doubling the lamp's lumens changed the film by %.4fx", ratio);
        /* Exactly two: radiance is linear in flux and the estimator is linear
         * in radiance, so this is not a statistical claim -- the two renders
         * share a seed and differ only by a scale. */
        CHECK_NEAR(ratio, 2.0, 1e-6);
    }

    SECTION("scenedesc: depth is derived, so it cannot drift");
    {
        OsSceneDesc d;
        os_scenedesc_preset(&d, OS_STAGE_DEPTH_RAIL);

        int id = os_scenedesc_next_object(&d, 0);
        CHECK(id >= 0);
        CHECK_NEAR(os_scenedesc_depth(&d, id), -d.obj[id].centre.z, 1e-12);

        /* Move it and the answer follows immediately -- there is no recorded
         * copy to forget to update. */
        d.obj[id].centre.z = -3.75;
        CHECK_NEAR(os_scenedesc_depth(&d, id), 3.75, 1e-12);

        /* A dead or unknown id has no depth, rather than a stale one. */
        CHECK(os_scenedesc_delete_object(&d, id));
        CHECK(os_scenedesc_depth(&d, id) < 0.0);
        CHECK(os_scenedesc_depth(&d, 999) < 0.0);
    }

    SECTION("scenedesc: the presets still describe the scenes they used to build");
    {
        OsSceneDesc rail, bok;
        os_scenedesc_preset(&rail, OS_STAGE_DEPTH_RAIL);
        os_scenedesc_preset(&bok,  OS_STAGE_BOKEH);

        /* Five targets and NOTHING ELSE, at the distances the depth-of-field
         * article depends on. No backdrop: a wall behind the subjects bounces
         * light onto them and gives every silhouette a second edge. */
        CHECK(os_scenedesc_count_objects(&rail) == 5);
        CHECK(os_scenedesc_count_lights(&rail) == 1);
        static const double WANT[] = { 1.0, 1.5, 2.0, 3.0, 5.0 };
        for (int i = 0; i < 5; ++i)
            CHECK_NEAR(os_scenedesc_depth(&rail, i), WANT[i], 1e-12);

        CHECK(os_scenedesc_count_lights(&bok) == 12);
        /* Bokeh is lamps against empty space -- no ground plane either. */
        CHECK(os_scenedesc_count_objects(&bok) == 0);

        OsStage st;
        CHECK(os_scenedesc_build(&rail, &st));
        CHECK(st.scene.nprims == 6);      /* five subjects plus the lamp's face */
        CHECK(st.scene.nlights == 1);
        /* The markers carry the ground truth, against the prim actually built. */
        CHECK_NEAR(os_stage_depth(&st, "2M"), 2.0, 1e-12);
        os_stage_free(&st);
    }
}
