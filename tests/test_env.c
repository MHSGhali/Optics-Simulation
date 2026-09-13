/* test_env.c — the ambient dome.
 *
 * WHY THIS SUITE EXISTS
 *   The dome is light with NO GEOMETRY. It is not a Prim and not an entry in
 *   Scene.lights, so nothing the rest of the suite checks reaches it: a dome
 *   that was half as bright as it claimed, or twice, would look completely
 *   plausible and would quietly move every exposure in the program.
 *
 *   So it is pinned against closed forms rather than against a picture. A
 *   Lambertian surface of reflectance rho under a uniform dome of radiance L
 *   returns exactly rho*L, and a surface facing that dome receives exactly
 *   pi*L. Both numbers are the whole content of the feature, and both are
 *   asserted here.
 *
 * THE ONE THAT MATTERS MOST
 *   "The answer does not depend on the bounce limit." The dome is reached by
 *   TWO sampling strategies -- next event toward the sky, and a BSDF-sampled
 *   ray that escapes -- and the MIS weights have to partition it exactly. Get
 *   them wrong and the scene gets brighter (double counted) or darker (a gap)
 *   with every bounce allowed, which reads as "ambient needs more bounces"
 *   rather than as a bug.
 */
#include "test.h"
#include "tests.h"

#include "opticsim/scenedesc.h"
#include "opticsim/trace.h"

#include "lightsim/core.h"
#include "lightsim/units.h"

#include <math.h>
#include <string.h>

/* The mean radiance a fixed fan of rays sees. Deterministic: the seed is the
 * ray index, so a change in the estimator shows up as a changed number and
 * never as a changed random sequence. */
static ls_real mean_radiance(const Scene *sc, const OsEnv *env, vec3 o, vec3 d,
                             ls_real lambda, int n, int depth) {
    ls_real sum = 0.0;
    for (int i = 0; i < n; ++i) {
        Rng rng = ls_rng_seed(12345u, (uint64_t)i + 1u);
        Ray r = { o, d, 0.0, HUGE_VAL };
        sum += os_trace_radiance(sc, env, r, lambda, &rng, depth);
    }
    return sum / (ls_real)n;
}

void os_test_env(void) {
    const ls_real LAMBDA = 550.0;

    SECTION("env: a dome authored in lux delivers that many lux");
    {
        /* The authoring conversion, end to end: the panel holds an
         * illuminance, the tracer wants a radiance, and the factor between
         * them is pi and nothing else. Getting this wrong by pi is a 3.14x
         * exposure error that looks exactly like "the ambient setting needs
         * different numbers from the lamps". */
        static const ls_real LUX[] = { 100.0, 2000.0, 50000.0 };
        for (int i = 0; i < 3; ++i) {
            OsSceneDesc d;
            os_scenedesc_preset(&d, OS_STAGE_DEPTH_RAIL);
            d.light_mode    = OS_LIGHT_AMBIENT;
            d.ambient_lux   = LUX[i];
            d.ambient_cct_k = 6500.0;

            OsStage st;
            CHECK(os_scenedesc_build(&d, &st));
            CHECK(st.env.on);

            /* pi * L, photometrically, is the illuminance on a surface facing
             * the dome -- which is the number that was typed in. */
            ls_real e_v = LS_PI * ls_photometric(&st.env.le);
            CHECK(fabs(e_v - LUX[i]) <= LUX[i] * 1e-6);

            /* And the lamps really are off: no Light, and no emissive prim
             * for one either. */
            CHECK(st.scene.nlights == 0);
            for (int p = 0; p < st.scene.nprims; ++p)
                CHECK(st.scene.prims[p].light_id < 0);
            os_stage_free(&st);
        }
        NOTE("pi * L_v == the authored lux, over 100 to 50000 lx");
    }

    SECTION("env: LAMPS mode builds no dome at all");
    {
        OsSceneDesc d;
        os_scenedesc_preset(&d, OS_STAGE_DEPTH_RAIL);
        d.ambient_lux = 9999.0;          /* set, but not selected */
        OsStage st;
        CHECK(os_scenedesc_build(&d, &st));
        CHECK(!st.env.on);
        CHECK(st.scene.nlights > 0);
        os_stage_free(&st);
    }

    SECTION("env: the camera does not photograph the dome, but everything is still lit by it");
    {
        /* TWO claims that have to hold together, which is why they are in one
         * section: the sky is not in the picture, and the sky still lights the
         * picture. Either alone is easy and useless -- switching the dome off
         * satisfies the first, and the old behaviour satisfied the second.
         *
         * The reason for the first is exposure. A dome bright enough to light
         * a scene outshines everything it lights, because rho is below one; in
         * shot it fills most of the frame, drives the exposure, and leaves no
         * setting at which the subjects are right and the background is not
         * clipped. See env.h. */
        OsEnv env;
        memset(&env, 0, sizeof env);
        env.on = true;
        env.le = ls_spectrum_const(3.0);
        ls_real le = ls_spectrum_at(&env.le, LAMBDA);
        CHECK(le > 0.0);

        /* ---- not photographed ----
         * Empty space in every direction, and the camera ray -- depth 0 -- has
         * to come back with nothing whatever the dome is doing. */
        Scene empty;
        memset(&empty, 0, sizeof empty);
        for (int i = 0; i < 8; ++i) {
            ls_real t = LS_TWO_PI * (ls_real)i / 8.0;
            vec3 dir = v3norm(v3(cos(t), 0.3, sin(t)));
            CHECK(mean_radiance(&empty, &env, v3(0,0,0), dir, LAMBDA, 4, 6) == 0.0);
        }
        /* Which is the same answer a dome that is OFF gives, and the same
         * answer no dome at all gives -- the background is black in all three
         * cases, and that is the point of the change. */
        OsEnv off = env; off.on = false;
        CHECK(mean_radiance(&empty, &off,  v3(0,0,0), v3(0,0,-1), LAMBDA, 4, 6) == 0.0);
        CHECK(mean_radiance(&empty, NULL, v3(0,0,0), v3(0,0,-1), LAMBDA, 4, 6) == 0.0);

        /* ---- and still lighting ----
         * A white Lambertian plane under the same dome. The camera ray HITS
         * it, so the dome reaches it by every path that is not the first one:
         * next-event estimation toward the sky at that vertex, and any bounce
         * that escapes afterwards. The closed form for a Lambertian under a
         * uniform dome is exactly rho * L, and nothing about that moved. */
        Material m[1];
        memset(m, 0, sizeof m);
        m[0].bsdf.kind = LS_BSDF_LAMBERT;
        m[0].bsdf.rho  = ls_spectrum_const(0.6);

        Prim p[1];
        memset(p, 0, sizeof p);
        p[0].kind = LS_PRIM_PLANE;
        p[0].c = v3(0.0, -1.0, 0.0); p[0].n = v3(0.0, 1.0, 0.0);
        p[0].mat_id = 0; p[0].light_id = -1; p[0].mesh_id = -1;

        Scene ground;
        memset(&ground, 0, sizeof ground);
        ground.prims = p; ground.nprims = 1;
        ground.mats  = m; ground.nmats  = 1;

        ls_real lit = mean_radiance(&ground, &env, v3(0.0, 2.0, 0.0),
                                    v3(0.0, -1.0, 0.0), LAMBDA, 4000, 2);
        NOTE("a 0.6 plane under a %.1f dome returns %.4f, closed form %.4f",
             le, lit, 0.6 * le);
        CHECK_NEAR(lit, 0.6 * le, 2e-3);
        /* Nonzero by a wide margin, so "black background" cannot be passing by
         * having quietly switched the dome off. */
        CHECK(lit > le * 0.5);
    }

    SECTION("env: a grey plane under a dome returns exactly rho * L");
    {
        /* THE closed form. One infinite Lambertian plane, nothing else, so
         * there is no interreflection to account for and no occlusion: the
         * plane sees the entire upper hemisphere. Radiance leaving it toward
         * the camera is rho * L_sky, with no pi anywhere -- the pi in the BSDF
         * and the pi in the hemispherical integral cancel, and this test is
         * what says they did. */
        static const ls_real RHO[] = { 0.2, 0.5, 0.9 };
        static const int DEPTHS[]  = { 2, 3, 6, 10 };

        for (int ri = 0; ri < 3; ++ri) {
            Material m;
            memset(&m, 0, sizeof m);
            m.bsdf.kind = LS_BSDF_LAMBERT;
            m.bsdf.rho  = ls_spectrum_const(RHO[ri]);

            Prim p;
            memset(&p, 0, sizeof p);
            p.kind = LS_PRIM_PLANE;
            p.c = v3(0.0, -1.0, 0.0);
            p.n = v3(0.0, 1.0, 0.0);
            p.mat_id = 0; p.light_id = -1; p.mesh_id = -1;

            Scene sc;
            memset(&sc, 0, sizeof sc);
            sc.prims = &p;  sc.nprims = 1;
            sc.mats  = &m;  sc.nmats  = 1;

            OsEnv env;
            memset(&env, 0, sizeof env);
            env.on = true;
            env.le = ls_spectrum_const(2.5);
            ls_real le   = ls_spectrum_at(&env.le, LAMBDA);
            ls_real want = RHO[ri] * le;

            /* Every bounce limit from 2 upward must give the SAME answer. A
             * double count or a gap in the MIS weights shows up here as a
             * trend with depth, and nowhere else. */
            for (int di = 0; di < 4; ++di) {
                ls_real got = mean_radiance(&sc, &env, v3(0.0, 2.0, 0.0),
                                            v3(0.0, -1.0, 0.0), LAMBDA,
                                            20000, DEPTHS[di]);
                ls_real err = fabs(got - want) / want;
                if (di == 0 || err > 0.01)
                    NOTE("rho %.1f, depth %2d: %.5f vs %.5f exact (%.2f%%)",
                         (double)RHO[ri], DEPTHS[di], (double)got,
                         (double)want, (double)(err * 100.0));
                CHECK(err < 0.02);
            }
        }
    }

    SECTION("env: the dome is occluded by geometry, not by distance");
    {
        /* A dome with no shadowing would be a constant added to every
         * surface, which is the "ambient term" of the 1980s and is not what
         * this is. Put a lid over the sample point and the sky it can see --
         * and so the radiance it returns -- must drop. */
        Material m[1];
        memset(m, 0, sizeof m);
        m[0].bsdf.kind = LS_BSDF_LAMBERT;
        m[0].bsdf.rho  = ls_spectrum_const(0.8);

        Prim p[2];
        memset(p, 0, sizeof p);
        p[0].kind = LS_PRIM_PLANE;
        p[0].c = v3(0.0, -1.0, 0.0); p[0].n = v3(0.0, 1.0, 0.0);
        p[0].mat_id = 0; p[0].light_id = -1; p[0].mesh_id = -1;
        /* The lid: a disk directly overhead, big enough to cover most of the
         * hemisphere as seen from the point below it. */
        p[1].kind = LS_PRIM_DISK;
        p[1].c = v3(0.0, -0.4, 0.0); p[1].n = v3(0.0, 1.0, 0.0); p[1].r = 3.0;
        p[1].mat_id = 0; p[1].light_id = -1; p[1].mesh_id = -1;

        OsEnv env;
        memset(&env, 0, sizeof env);
        env.on = true;
        env.le = ls_spectrum_const(2.5);

        Scene open_sky;
        memset(&open_sky, 0, sizeof open_sky);
        open_sky.prims = p; open_sky.nprims = 1;
        open_sky.mats  = m; open_sky.nmats  = 1;

        Scene lidded = open_sky;
        lidded.nprims = 2;

        ls_real a = mean_radiance(&open_sky, &env, v3(0.0, 2.0, 0.0),
                                  v3(0.0, -1.0, 0.0), LAMBDA, 4000, 2);
        ls_real b = mean_radiance(&lidded, &env, v3(0.0, -0.9, 0.0),
                                  v3(0.0, -1.0, 0.0), LAMBDA, 4000, 2);
        NOTE("open sky %.4f, under a lid %.4f", (double)a, (double)b);
        CHECK(b < a * 0.25);
    }

    SECTION("env: a subject under the dome returns less than the dome puts on it");
    {
        /* A Lambertian surface can only ever return rho * L with rho < 1, so
         * no subject can be brighter than the sky lighting it. Getting this
         * backwards is the visible symptom of light being counted twice on the
         * way out of a surface, or of an "ambient term" added to shading
         * instead of traced.
         *
         * MEASURED AGAINST THE DOME ITSELF, not against the background. This
         * section used to photograph a patch of empty sky and compare with
         * that, which worked only because the camera could see the dome; it
         * cannot any more, and it should not -- the black behind these
         * subjects is the whole point of the change. The dome's radiance is
         * known exactly, from the description it was built from, so comparing
         * with the number is both simpler and stricter than comparing with a
         * traced estimate of it. */
        OsSceneDesc d;
        os_scenedesc_preset(&d, OS_STAGE_DEPTH_RAIL);
        d.light_mode  = OS_LIGHT_AMBIENT;
        d.ambient_lux = 2000.0;
        OsStage st;
        CHECK(os_scenedesc_build(&d, &st));

        ls_real sky = ls_spectrum_at(&st.env.le, LAMBDA);
        CHECK(sky > 0.0);

        /* And the background really is black now, in the same scene, on a ray
         * aimed over the targets' heads. */
        CHECK(mean_radiance(&st.scene, &st.env, v3(0,0,0),
                            v3norm(v3(0.0, 0.6, -1.0)), LAMBDA, 8, 8) == 0.0);

        for (int i = 0; i < d.nobj; ++i) {
            if (!d.obj[i].alive || d.obj[i].kind != OS_OBJ_SPHERE) continue;
            /* Straight at this subject's centre. */
            vec3 dir = v3norm(d.obj[i].centre);
            ls_real L = mean_radiance(&st.scene, &st.env, v3(0,0,0), dir,
                                      LAMBDA, 20000, 8);
            NOTE("%-5s returns %.5f of a sky of %.5f (%.2f)",
                 d.obj[i].name, (double)L, (double)sky, (double)(L / sky));
            /* Lit, and never more than the sky that lit it. */
            CHECK(L > 0.0);
            CHECK(L < sky);
        }
        os_stage_free(&st);
    }

    SECTION("env: switching it off leaves the lamp estimator untouched");
    {
        /* The dome joins the uniform light choice, so every 1/nlights in the
         * estimator became 1/(nlights+1). If that arithmetic leaked into the
         * lamps-only path, every existing render would have shifted. A dome
         * that is off must be indistinguishable from no dome at all -- not
         * close, IDENTICAL. */
        OsSceneDesc d;
        os_scenedesc_preset(&d, OS_STAGE_DEPTH_RAIL);
        OsStage st;
        CHECK(os_scenedesc_build(&d, &st));

        OsEnv off;
        memset(&off, 0, sizeof off);
        off.le = ls_spectrum_const(9.0);   /* bright, but off */

        /* And a dome that is ON but at zero radiance, which is the other way
         * to say nothing: it must not become an extra strategy either, or the
         * lamps would be sampled a fraction of the time less often for no
         * reason. */
        OsEnv zero;
        memset(&zero, 0, sizeof zero);
        zero.on = true;
        zero.le = ls_spectrum_zero();

        for (int i = 0; i < 24; ++i) {
            ls_real t = LS_TWO_PI * (ls_real)i / 24.0;
            vec3 dir = v3norm(v3(0.25 * cos(t), 0.25 * sin(t), -1.0));
            ls_real a = mean_radiance(&st.scene, NULL,  v3(0,0,0), dir, LAMBDA, 32, 5);
            ls_real b = mean_radiance(&st.scene, &off,  v3(0,0,0), dir, LAMBDA, 32, 5);
            ls_real c = mean_radiance(&st.scene, &zero, v3(0,0,0), dir, LAMBDA, 32, 5);
            CHECK(a == b);
            CHECK(a == c);
        }
        os_stage_free(&st);
    }

    SECTION("env: the dome and the lamps add up, and neither is counted twice");
    {
        /* Both present at once. The estimator picks ONE strategy per vertex
         * out of nlights+1, so the two must sum -- a render lit by both is
         * the lamp-only render plus the dome-only render, to within Monte
         * Carlo error. This is the property that a naive "always sample the
         * sky as well" would break by over-counting the lamps. */
        OsSceneDesc d;
        os_scenedesc_preset(&d, OS_STAGE_DEPTH_RAIL);
        OsStage st;
        CHECK(os_scenedesc_build(&d, &st));      /* lamps on */

        OsEnv sky;
        memset(&sky, 0, sizeof sky);
        sky.on = true;
        sky.le = ls_spectrum_const(0.02);

        OsEnv none;
        memset(&none, 0, sizeof none);

        /* A direct view of the 2 m target, where a lamp and the sky both
         * reach and one bounce carries nearly all of it. */
        vec3 dir = v3(0.0, 0.0, -1.0);
        ls_real lamps = mean_radiance(&st.scene, &none, v3(0,0,0), dir, LAMBDA, 60000, 2);
        ls_real dome  = 0.0, both = 0.0;

        /* The dome-only case needs the lamps gone from the SCENE, not just
         * from the estimator -- their emissive faces are still geometry. */
        OsSceneDesc da = d;
        da.light_mode = OS_LIGHT_AMBIENT;
        OsStage sa;
        CHECK(os_scenedesc_build(&da, &sa));
        dome = mean_radiance(&sa.scene, &sky, v3(0,0,0), dir, LAMBDA, 60000, 2);
        both = mean_radiance(&st.scene, &sky, v3(0,0,0), dir, LAMBDA, 60000, 2);

        NOTE("lamps %.4e + dome %.4e = %.4e, together %.4e",
             (double)lamps, (double)dome, (double)(lamps + dome), (double)both);
        CHECK(fabs(both - (lamps + dome)) <= 0.03 * (lamps + dome));

        os_stage_free(&sa);
        os_stage_free(&st);
    }
}
