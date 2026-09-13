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
        OsSceneDesc rail, ring, bok;
        os_scenedesc_preset(&rail, OS_STAGE_DEPTH_RAIL);
        os_scenedesc_preset(&ring, OS_STAGE_DEPTH_RING);
        os_scenedesc_preset(&bok,  OS_STAGE_BOKEH);

        /* Five targets and NOTHING ELSE, at the distances the depth-of-field
         * article depends on. No backdrop: a wall behind the subjects bounces
         * light onto them and gives every silhouette a second edge.
         *
         * BOTH rails, and to the same depths -- they are one experiment in two
         * arrangements, so anything that differs between them other than where
         * the targets sit across the frame is a bug in one of them. */
        static const double WANT[] = { 1.0, 1.5, 2.0, 3.0, 5.0 };
        const OsSceneDesc *both[2] = { &rail, &ring };
        for (int k = 0; k < 2; ++k) {
            CHECK(os_scenedesc_count_objects(both[k]) == 5);
            CHECK(os_scenedesc_count_lights(both[k]) == 1);
            for (int i = 0; i < 5; ++i)
                CHECK_NEAR(os_scenedesc_depth(both[k], i), WANT[i], 1e-12);
        }

        /* Bokeh is no longer lamps against nothing: it has subjects now, in
         * four depth layers, and the section below is about what those are
         * for. Here it is only the shape of the thing -- objects AND lights,
         * where the old scene had lights alone. */
        CHECK(os_scenedesc_count_objects(&bok) > 0);
        CHECK(os_scenedesc_count_lights(&bok) > 0);

        OsStage st;
        CHECK(os_scenedesc_build(&rail, &st));
        CHECK(st.scene.nprims == 6);      /* five subjects plus the lamp's face */
        CHECK(st.scene.nlights == 1);
        /* The markers carry the ground truth, against the prim actually built. */
        CHECK_NEAR(os_stage_depth(&st, "2M"), 2.0, 1e-12);
        os_stage_free(&st);
    }

    SECTION("rail and ring: one experiment, two arrangements");
    {
        /* The pair exists to isolate FIELD POSITION, so the test is that field
         * position is the only thing that differs between them. Everything
         * else -- depths, sizes on the sensor, colours, lighting -- has to
         * match, or a comparison between the two pictures is measuring
         * something nobody intended. */
        OsSceneDesc rail, ring;
        os_scenedesc_preset(&rail, OS_STAGE_DEPTH_RAIL);
        os_scenedesc_preset(&ring, OS_STAGE_DEPTH_RING);

        for (int i = 0; i < 5; ++i) {
            const OsObject *a = &rail.obj[i], *b = &ring.obj[i];
            CHECK(a->alive && b->alive);
            CHECK(strcmp(a->name, b->name) == 0);
            /* Same depth, same colour. */
            CHECK_NEAR(a->centre.z, b->centre.z, 1e-12);
            for (int c = 0; c < 3; ++c) CHECK_NEAR(a->rgb[c], b->rgb[c], 1e-12);
        }

        /* ---- the ring is a ring ----
         *
         * Every target at the SAME angular radius, which is the entire reason
         * the arrangement exists: equal field radius means equal field
         * aberration, and equal field aberration cancels out of every
         * comparison between them. One target nudged off the circle would
         * quietly reintroduce the thing the ring was built to remove. */
        ls_real r0 = 0.0;
        for (int i = 0; i < 5; ++i) {
            const OsObject *o = &ring.obj[i];
            ls_real z = -o->centre.z;
            ls_real ang = sqrt(o->centre.x * o->centre.x
                             + o->centre.y * o->centre.y) / z;
            if (i == 0) r0 = ang; else CHECK_NEAR(ang, r0, 1e-12);
            /* And the same angular SIZE, so they land the same size on the
             * sensor whatever depth they sit at. */
            CHECK_NEAR(o->radius / z, ring.obj[0].radius / (-ring.obj[0].centre.z),
                       1e-12);
        }
        NOTE("the ring sits at %.4f rad off axis, all five", r0);
        CHECK(r0 > 0.0);

        /* Spread evenly around the circle, so no two crowd each other. On a
         * regular pentagon neighbours are 2*R*sin(36 deg) = 1.176*R apart, and
         * that has to clear two radii with room left over. */
        ls_real ang_rad = ring.obj[0].radius / (-ring.obj[0].centre.z);
        ls_real gap = 2.0 * r0 * sin(36.0 * LS_PI / 180.0) - 2.0 * ang_rad;
        NOTE("neighbours clear each other by %.4f rad", gap);
        CHECK(gap > 0.005);

        /* ---- the row is a row ----
         *
         * Flat, spread sideways, and reaching much further off axis than the
         * ring does -- which is the honest failing this arrangement is kept
         * to demonstrate rather than a defect to fix. */
        ls_real widest = 0.0;
        for (int i = 0; i < 5; ++i) {
            CHECK_NEAR(rail.obj[i].centre.y, 0.0, 1e-12);
            ls_real ang = fabs(rail.obj[i].centre.x) / (-rail.obj[i].centre.z);
            if (ang > widest) widest = ang;
        }
        NOTE("the row reaches %.4f rad off axis, %.1fx the ring's radius",
             widest, widest / r0);
        CHECK(widest > r0 * 2.0);

        /* ---- equal luminance, which is the other half of "identical" ----
         *
         * The eye reads brightness as sharpness, so a target that is darker
         * than its neighbours is a second difference sitting on top of the one
         * being measured. The colouring this replaced made the 2 m target --
         * the one the default focus picks out -- 2.8x darker than the rest. */
        ls_real lo = 1e9, hi = 0.0;
        for (int i = 0; i < 5; ++i) {
            const ls_real *c = ring.obj[i].rgb;
            ls_real y = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
            if (y < lo) lo = y;
            if (y > hi) hi = y;
        }
        NOTE("target luminance spans %.4f to %.4f (%.2f%%)",
             lo, hi, 100.0 * (hi / lo - 1.0));
        CHECK(hi / lo < 1.01);

        /* And they are actually different colours, not five names for grey --
         * the other way to make five targets indistinguishable. */
        int distinct = 0;
        for (int i = 0; i < 5; ++i)
            for (int j = i + 1; j < 5; ++j) {
                ls_real d2 = 0.0;
                for (int c = 0; c < 3; ++c) {
                    ls_real dc = ring.obj[i].rgb[c] - ring.obj[j].rgb[c];
                    d2 += dc * dc;
                }
                if (d2 > 0.04) distinct++;
            }
        CHECK(distinct == 10);          /* all ten pairs, not just some */
    }

    SECTION("grid: a chart that is straight, so the lens can bend it");
    {
        /* The distortion target, and the whole burden on the SCENE is that it
         * is geometrically honest: the dots must be collinear and evenly
         * spaced in object space, on one plane, or a bow in the picture is the
         * chart's fault rather than the lens's. Whether the lens then bends it
         * is os_lens_distortion_pct's business and is tested in test_lens. */
        OsSceneDesc d;
        os_scenedesc_preset(&d, OS_STAGE_GRID);

        const int COLS = 9, ROWS = 7;
        CHECK(os_scenedesc_count_objects(&d) == COLS * ROWS);

        /* ---- one plane ---- */
        ls_real z0 = d.obj[0].centre.z;
        for (int i = 0; i < d.nobj; ++i) {
            if (!d.obj[i].alive) continue;
            CHECK_NEAR(d.obj[i].centre.z, z0, 1e-12);
        }
        CHECK(z0 < 0.0);

        /* ---- collinear rows and columns, to the last bit ----
         *
         * Row-major order, so obj[r*COLS + c] is row r column c. Every dot in
         * a row shares a y, every dot in a column shares an x. This is the
         * property the whole target rests on. */
        for (int r = 0; r < ROWS; ++r)
            for (int c = 0; c < COLS; ++c) {
                const OsObject *o = &d.obj[r * COLS + c];
                CHECK_NEAR(o->centre.y, d.obj[r * COLS].centre.y, 1e-12);
                CHECK_NEAR(o->centre.x, d.obj[c].centre.x, 1e-12);
            }

        /* ---- evenly spaced, which is what makes uneven spacing meaningful ----
         *
         * Even in METRES on a plane, which is even in tan(theta), which a
         * rectilinear lens images to even spacing on the film. Anything else
         * in the picture is the lens. */
        ls_real dx = d.obj[1].centre.x - d.obj[0].centre.x;
        ls_real dy = d.obj[COLS].centre.y - d.obj[0].centre.y;
        CHECK(dx > 0.0 && dy > 0.0);
        for (int c = 1; c < COLS; ++c)
            CHECK_NEAR(d.obj[c].centre.x - d.obj[c - 1].centre.x, dx, 1e-12);
        for (int r = 1; r < ROWS; ++r)
            CHECK_NEAR(d.obj[r * COLS].centre.y - d.obj[(r - 1) * COLS].centre.y,
                       dy, 1e-12);

        /* Centred on the axis, so the middle dot marks where distortion is
         * zero by definition. */
        CHECK_NEAR(d.obj[(ROWS / 2) * COLS + COLS / 2].centre.x, 0.0, 1e-12);
        CHECK_NEAR(d.obj[(ROWS / 2) * COLS + COLS / 2].centre.y, 0.0, 1e-12);

        /* ---- the dots do not touch ---- */
        ls_real rad = d.obj[0].radius;
        CHECK(rad > 0.0);
        CHECK(dy - 2.0 * rad > 0.05);
        NOTE("grid: %dx%d dots, %.3f x %.3f m apart, radius %.3f m, on one "
             "plane at %.2f m", COLS, ROWS, dx, dy, rad, -z0);

        /* ---- and it fills a frame worth looking at ----
         *
         * Sized for 35 mm: the chart's angular extent is fixed once its metres
         * are, and no chart of this many dots can be dense at 100 mm and full
         * at 24. The check is that the corner reaches a field angle where
         * these designs actually distort -- below about 0.3 rad there is
         * nothing to see on any of them. */
        ls_real hw = d.obj[COLS - 1].centre.x, hh = d.obj[(ROWS-1)*COLS].centre.y;
        ls_real corner = sqrt(hw * hw + hh * hh) / (-z0);
        NOTE("grid: the corner sits %.3f rad off axis", corner);
        CHECK(corner > 0.3);

        /* ---- and no lamp is in shot, at ANY focal length ----
         *
         * Both sit BEHIND the camera, at positive z. A camera ray leaves the
         * film travelling toward -z and can never reach them, whatever the
         * focal length -- which is a stronger guarantee than the bokeh scene's
         * overhead rig can give, and the right one for a chart, because it
         * also puts the light frontal so no dot has a terminator to be
         * mistaken for a shift in position. */
        int nlit = 0;
        for (int i = 0; i < d.nlit; ++i) {
            if (!d.lit[i].alive) continue;
            nlit++;
            CHECK(d.lit[i].centre.z > 0.0);
        }
        CHECK(nlit == 2);
    }

    SECTION("bokeh: a field of depth, and one focus setting that sorts it");
    {
        /* WHAT THIS STAGE IS, and why it is worth testing.
         *
         * It is a field of ordinary objects spread from half a metre to
         * fourteen, one thin layer of which is sharp -- so the blur can be
         * watched growing in both directions from the focus plane, on surfaces
         * that have shading and colour and occlusion rather than on bare
         * highlights. The checks below pin the parts of that a nudged number in
         * the table would quietly break and no test could otherwise see.
         *
         * The ten background lamps that used to make blur DISCS are gone. The
         * iris shape they showed is a real subject, and it is checked where it
         * belongs -- on the pupil, in test_lens -- rather than requiring this
         * scene to keep a lamp grid in it. */
        OsSceneDesc d;
        os_scenedesc_preset(&d, OS_STAGE_BOKEH);

        /* The focus the docs render uses. Everything below is stated relative
         * to it rather than to an absolute distance, so moving the whole scene
         * would not silently invalidate the test. */
        const ls_real FOCUS = 1.2;

        /* ---- no light is in shot, AT ANY FOCAL LENGTH ----
         *
         * THE structural rule, and the one a dragged lamp breaks first. A
         * source inside the frame is photographed, and a source of these
         * fluxes photographed is a blown white ellipse sitting on top of
         * whatever it was lighting.
         *
         * Checked across the whole focal range the viewer offers, because the
         * first arrangement passed at 100 mm and failed everywhere below it: a
         * 36 x 24 mm frame subtends +/-0.18 by +/-0.12 rad at 100 mm but
         * +/-1.50 by +/-1.00 at 12 mm, so lamps parked just outside a long
         * lens's frame sail into a short one's. Testing the single default
         * focal length is what let that ship. */
        static const ls_real FOCAL[] = { 12.0, 24.0, 35.0, 50.0, 100.0, 400.0 };
        int nlit = 0;
        ls_real tightest = HUGE_VAL;
        for (int i = 0; i < d.nlit; ++i) {
            if (!d.lit[i].alive) continue;
            nlit++;
            ls_real z = -d.lit[i].centre.z;
            CHECK(z > 0.0);
            for (size_t k = 0; k < sizeof FOCAL / sizeof FOCAL[0]; ++k) {
                /* Half-angles of a 36 x 24 mm frame at this focal length. */
                ls_real hx = 18.0 / FOCAL[k], hy = 12.0 / FOCAL[k];
                bool in_shot = fabs(d.lit[i].centre.x) < hx * z
                            && fabs(d.lit[i].centre.y) < hy * z;
                CHECK(!in_shot);
            }
            /* How wide the lens could go before this one appears -- reported
             * so the margin is visible rather than merely asserted. */
            ls_real ax = fabs(d.lit[i].centre.x) / z;
            ls_real ay = fabs(d.lit[i].centre.y) / z;
            ls_real fx = ax > 0.0 ? 18.0 / ax : HUGE_VAL;
            ls_real fy = ay > 0.0 ? 12.0 / ay : HUGE_VAL;
            ls_real f  = fx < fy ? fx : fy;
            if (f < tightest) tightest = f;
        }
        NOTE("%d lamps, all out of frame down to %.1f mm -- past the 12 mm the "
             "viewer allows", nlit, tightest);
        CHECK(tightest < 12.0);
        /* Enough of them to cover the depth range: one key cannot, because
         * illuminance falls as one over r squared and by 14 m a lamp placed for
         * the subject has a four-hundredth of its output left. */
        CHECK(nlit >= 3);

        /* ---- the field is deep, and populated all the way through ---- */
        int near = 0, at = 0, far = 0, deep = 0;
        ls_real nearest = HUGE_VAL, furthest = 0.0;
        for (int i = 0; i < d.nobj; ++i) {
            if (!d.obj[i].alive) continue;
            ls_real z = os_scenedesc_depth(&d, i);
            CHECK(z > 0.0);                     /* nothing behind the camera  */
            if (z < nearest)  nearest = z;
            if (z > furthest) furthest = z;
            if      (z < FOCUS * 0.75) near++;
            else if (z > FOCUS * 6.0)  deep++;
            else if (z > FOCUS * 1.5)  far++;
            else                       at++;
        }
        /* Counted by DEPTH alone -- "at the focus depth" is not the same as
         * "sharp", since the peripheral field has objects at every depth and
         * none of them are near the axis. The sharp count is nsharp, below. */
        NOTE("bokeh field: %.2f m to %.1f m -- %d foreground, %d at the focus "
             "depth, %d behind, %d deep", nearest, furthest, near, at, far, deep);
        CHECK(near >= 2);      /* a foreground, blurred on the OTHER side     */
        CHECK(at   >= 3);      /* something sharp, or "blurred" means nothing */
        CHECK(far  >= 4);      /* the middle of the range, not just its ends  */
        CHECK(deep >= 3);      /* past the knee, where blur stops growing     */
        CHECK(furthest / nearest > 20.0);

        /* ---- various in size, and in APPARENT size ----
         *
         * Two ways to be the same, and both would spoil it. Equal physical
         * radii make a scene of one ball at many distances; equal angular radii
         * make a wall of identical discs. The table is authored in angles, so
         * the second is the one to guard, and the first follows from the depth
         * spread above. */
        ls_real amin = HUGE_VAL, amax = 0.0, rmin = HUGE_VAL, rmax = 0.0;
        for (int i = 0; i < d.nobj; ++i) {
            if (!d.obj[i].alive) continue;
            ls_real z = os_scenedesc_depth(&d, i);
            ls_real a = d.obj[i].radius / z;
            if (a < amin) amin = a;
            if (a > amax) amax = a;
            if (d.obj[i].radius < rmin) rmin = d.obj[i].radius;
            if (d.obj[i].radius > rmax) rmax = d.obj[i].radius;
        }
        NOTE("radii: %.3f-%.3f m physical (%.1fx), %.3f-%.3f rad apparent (%.1fx)",
             rmin, rmax, rmax / rmin, amin, amax, amax / amin);
        CHECK(rmax / rmin > 5.0);
        CHECK(amax / amin > 3.0);

        /* ---- and various in colour ----
         *
         * Saturated enough that the hue survives the rgb -> spectrum uplift,
         * which pulls everything toward neutral, and spread widely enough that
         * no two objects read as the same paint. */
        int coloured = 0, framed = 0, pairs = 0, close = 0, distinct = 0;
        for (int i = 0; i < d.nobj; ++i) {
            if (!d.obj[i].alive) continue;
            ls_real lo = d.obj[i].rgb[0], hi = d.obj[i].rgb[0];
            for (int c = 1; c < 3; ++c) {
                lo = d.obj[i].rgb[c] < lo ? d.obj[i].rgb[c] : lo;
                hi = d.obj[i].rgb[c] > hi ? d.obj[i].rgb[c] : hi;
            }
            if (hi - lo > 0.1) coloured++;

            /* Distinct across the whole field, counted once each. */
            bool seen = false;
            for (int j = 0; j < i; ++j) {
                if (!d.obj[j].alive) continue;
                ls_real d2 = 0.0;
                for (int c = 0; c < 3; ++c) {
                    ls_real dc = d.obj[i].rgb[c] - d.obj[j].rgb[c];
                    d2 += dc * dc;
                }
                if (d2 < 0.01) seen = true;
            }
            if (!seen) distinct++;

            /* And PAIRWISE distinct among the ones that share the 100 mm
             * frame, which is where two identical paints would actually be
             * seen side by side. The peripheral field cycles a palette and
             * repeats it, which is fine at 0.5 rad off axis and would be a
             * waste of thirty-two hand-picked colours. */
            ls_real zi = os_scenedesc_depth(&d, i);
            if (!(fabs(d.obj[i].centre.x) < 0.18 * zi
               && fabs(d.obj[i].centre.y) < 0.12 * zi)) continue;
            framed++;
            for (int j = 0; j < i; ++j) {
                if (!d.obj[j].alive) continue;
                ls_real zj = os_scenedesc_depth(&d, j);
                if (!(fabs(d.obj[j].centre.x) < 0.18 * zj
                   && fabs(d.obj[j].centre.y) < 0.12 * zj)) continue;
                ls_real d2 = 0.0;
                for (int c = 0; c < 3; ++c) {
                    ls_real dc = d.obj[i].rgb[c] - d.obj[j].rgb[c];
                    d2 += dc * dc;
                }
                pairs++;
                if (d2 < 0.01) close++;
            }
        }
        NOTE("%d of %d objects carry a hue, %d distinct colours; of the %d in "
             "the 100 mm frame, %d of %d pairs are near-identical",
             coloured, os_scenedesc_count_objects(&d), distinct, framed,
             close, pairs);
        CHECK(coloured >= 12);
        CHECK(distinct >= 20);
        CHECK(close == 0);

        /* ---- and the optics sort the field the way the table says ----
         *
         * The layers are only worth having if the LENS separates them, so this
         * is asked of os_lens_coc_mm at the focus the docs render uses. A
         * SEPARATION rather than an absolute: the worst-blurred thing on the
         * focus plane is several times sharper than the best-focused thing off
         * it. An absolute threshold would be measuring the lens -- at 1.2 m
         * this achromat is aberration-limited and its on-axis spot is already
         * wider than the 0.030 mm the panel calls sharp. */
        char why[256];
        OsLens L;
        CHECK(os_lens_build(&L, OS_LENS_ACHROMAT_100, 100.0, 5.0, why, sizeof why));
        L.blades = 6;
        CHECK(os_lens_set_fnumber(&L, 5.0));
        CHECK(os_lens_focus(&L, FOCUS));

        /* "In the sharp layer" takes BOTH a depth and a field angle, because
         * sharpness does. The achromat covers a 20 mm image circle, so past
         * 0.10 rad off axis it is soft no matter how well it is focused -- and
         * the peripheral field has objects at every depth including this one.
         * Testing on depth alone would ask a sphere 0.5 rad off axis to be
         * sharp because it happens to sit at 1.6 m, which is a question about
         * the test rather than about the scene. */
        int nsharp = 0;
        ls_real worst_sharp = 0.0, best_blur = HUGE_VAL;
        for (int i = 0; i < d.nobj; ++i) {
            if (!d.obj[i].alive) continue;
            ls_real z   = os_scenedesc_depth(&d, i);
            ls_real coc = os_lens_coc_mm(&L, z);
            ls_real h   = sqrt(d.obj[i].centre.x * d.obj[i].centre.x
                             + d.obj[i].centre.y * d.obj[i].centre.y);
            bool on_axis = h / z < 0.10;
            if (z >= FOCUS * 0.75 && z <= FOCUS * 1.5 && on_axis) {
                nsharp++;
                if (coc > worst_sharp) worst_sharp = coc;
                /* And "sharp" is the traced ray's opinion too, not only the
                 * paraxial model's. */
                CHECK(os_lens_spot_mm(&L, z, h, 11) < 0.30);
            } else if (z < FOCUS * 0.75 || z > FOCUS * 1.5) {
                /* Only DEPTH disqualifies something from being the reference
                 * for "blurred". A near-axis object at the focus distance that
                 * is soft for a field reason would be neither, and there are
                 * none -- nsharp below is the count that says so. */
                if (coc < best_blur) best_blur = coc;
            }
        }
        CHECK(nsharp == 3);
        NOTE("focused at %.1f m: the sharp layer blurs to at most %.3f mm, "
             "everything else to at least %.3f mm", FOCUS, worst_sharp, best_blur);
        CHECK(best_blur > worst_sharp * 4.0);

        /* ---- the far end is past the knee ----
         *
         * Blur asymptotes: an object at infinity images a fixed distance from
         * the focused one, so past a certain depth extra distance stops buying
         * softness. The deep layer is there to sit on the far side of that, and
         * the check is that it does -- its blur is within a few per cent of the
         * limit, so those objects differ from each other in BRIGHTNESS rather
         * than in sharpness. Lose that and the scene's back half is just more
         * of its middle. */
        ls_real at_infinity = os_lens_coc_mm(&L, HUGE_VAL);
        ls_real deepest = os_lens_coc_mm(&L, furthest);
        NOTE("blur at %.0f m is %.3f mm against %.3f mm at infinity (%.0f%% of "
             "the way there)", furthest, deepest, at_infinity,
             100.0 * deepest / at_infinity);
        CHECK(deepest > at_infinity * 0.9);
        CHECK(deepest < at_infinity);
    }

    SECTION("bokeh: the whole field shares one exposure");
    {
        /* The stage used to need an exposure of its own. Twelve sources of
         * 5800 lm against black put the brightest pixel about 1400x over white
         * at the default gain of 100, so the docs render passed --exposure 0.2
         * -- and at 0.2 anything that was not a lamp was black, which is a
         * large part of why nothing else was in the scene.
         *
         * Now there are no bare sources at all, and the four lamps are aimed at
         * the rail's own level, so the default gain serves this stage as it
         * serves the others: nothing clips, and the deep end is still visible
         * rather than crushed. Both halves of that are load-bearing -- a scene
         * that fits the range by being uniformly grey would pass a check on
         * either one alone -- so both are checked, along with the spread
         * between them. */
        const int W = 96, H = 64;
        const ls_real EXPOSURE = 100.0;     /* the viewer's default, unmodified */

        OsSceneDesc d;
        os_scenedesc_preset(&d, OS_STAGE_BOKEH);
        OsStage st;
        CHECK(os_scenedesc_build(&d, &st));

        char why[256];
        OsCamera cam;
        CHECK(os_camera_build(&cam, OS_LENS_ACHROMAT_100, 100.0, 5.0,
                              36.0, W, H, why, sizeof why));
        cam.lens.blades = 6;
        CHECK(os_lens_focus(&cam.lens, 1.2));
        os_camera_refresh(&cam);
        os_camera_look_at(&cam, st.cam_eye, st.cam_target, v3(0, 1, 0));

        Film film;
        CHECK(ls_film_init(&film, W, H));
        OsRenderOpts opt = { 128, 4, 0, 0x853C49E6748FEA9Bull };
        os_render_pass(&film, &cam, &st, &opt, 0);

        ls_real hi = 0.0, sum = 0.0;
        int lit = 0, blown = 0;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                Spectrum sp = ls_film_mean(&film, x, y);
                ls_real Y = ls_spectrum_to_xyz(&sp).y * EXPOSURE;
                if (Y > hi) hi = Y;
                if (Y > 1.0) blown++;
                sum += Y;
                if (Y > 0.02) lit++;           /* plainly not background */
            }
        ls_real mean = sum / (ls_real)(W * H);
        NOTE("at the default exposure: peak %.2f, mean %.4f, %d%% of the frame "
             "above 0.02, %d pixels over white", hi, mean,
             100 * lit / (W * H), blown);

        /* NOTHING clips. There is no highlight in this scene to justify one --
         * every surface is a Lambertian reflector under a placed lamp, so a
         * blown pixel here would mean a lamp too close or too strong rather
         * than anything the picture is about.
         *
         * And the margin is bigger than it looks: a MAX over six thousand
         * pixels is the noisiest statistic there is, so at this sample count
         * the peak reads about 0.88 where the converged value is 0.64. The
         * claim survives the noise, which is the point of making it on the
         * max rather than on a percentile. */
        CHECK(hi < 1.0);
        CHECK(blown == 0);
        /* But it uses the range rather than hiding in the bottom of it. */
        CHECK(hi > 0.4);
        /* And a third of the frame is subject that can be SEEN rather than
         * black -- the check the old lamps-against-nothing scene could never
         * have passed, and the one that fails if a lamp is lost or the field
         * collapses toward the camera. */
        CHECK(lit * 100 / (W * H) > 25);
        CHECK(mean > 0.02);

        ls_film_free(&film);
        os_camera_free(&cam);
        os_stage_free(&st);
    }
}
