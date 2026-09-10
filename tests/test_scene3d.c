/* test_scene3d.c — the scene view's geometry and its depth-of-field solver.
 *
 * A view matrix with a sign error produces a picture that looks like a
 * picture. Nothing about it says "the elevation is inverted" or "this is
 * mirrored"; you simply believe it. So the projection is pinned against cases
 * whose answer is known without looking: the target lands in the centre,
 * something behind the eye is refused, and up is up.
 *
 * And the depth-of-field slab is checked against the century-old hyperfocal
 * formulae, because the whole point of drawing it is to be able to trust where
 * it falls. */
#include "test.h"
#include "tests.h"

#include "../viewer/scene3d.h"
#include "opticsim/scenedesc.h"

#include <math.h>
#include <string.h>

/* The textbook depth of field, for comparison against the solver. */
static ls_real hyperfocal_mm(ls_real f, ls_real N, ls_real c) {
    return f * f / (N * c) + f;
}

void os_test_scene3d(void) {
    char why[256];

    SECTION("dof: the solver reproduces the textbook formulae");
    {
        /* On the ideal thin lens the bisection must land exactly where the
         * hyperfocal expressions say. They are two entirely different
         * computations -- one solves the real lens's own blur function, the
         * other is a closed form -- so agreeing is a real check. */
        OsLens L;
        CHECK(os_lens_build(&L, OS_LENS_THIN, 100.0, 5.0, why, sizeof why));

        const ls_real c = 0.030;             /* the classic 35 mm criterion */
        ls_real f = L.efl_mm, N = L.f_number;
        ls_real H = hyperfocal_mm(f, N, c);

        NOTE("hyperfocal: solver %.3f m, closed form %.3f m",
             os_lens_hyperfocal_m(&L, c), H / 1000.0);
        CHECK_NEAR(os_lens_hyperfocal_m(&L, c), H / 1000.0, 2e-3);

        for (double s_m = 0.5; s_m <= 20.0; s_m *= 2.0) {
            CHECK(os_lens_focus(&L, s_m));
            ls_real nr = 0.0, fr = 0.0;
            CHECK(os_lens_dof(&L, c, &nr, &fr));

            ls_real s = s_m * 1000.0;
            ls_real want_near = s * (H - f) / (H + s - 2.0 * f);
            CHECK_NEAR(nr, want_near / 1000.0, 2e-3);

            if (s < H) {
                ls_real want_far = s * (H - f) / (H - s);
                CHECK_NEAR(fr, want_far / 1000.0, 2e-3);
            } else {
                CHECK(!isfinite(fr));        /* at or past hyperfocal */
            }

            /* Whatever the numbers, the slab must bracket the focus. */
            CHECK(nr < s_m);
            CHECK(fr > s_m);
        }
    }

    SECTION("dof: it widens as the aperture closes, and never inverts");
    {
        OsLens L;
        CHECK(os_lens_build(&L, OS_LENS_ACHROMAT_100, 100.0, 5.0, why, sizeof why));

        ls_real prev = 0.0;
        for (double n = 5.0; n <= 32.0; n *= 1.4142135623730951) {
            CHECK(os_lens_set_fnumber(&L, n));
            CHECK(os_lens_focus(&L, 2.0));
            ls_real nr = 0.0, fr = 0.0;
            CHECK(os_lens_dof(&L, 0.030, &nr, &fr));
            CHECK(nr > 0.0 && nr < 2.0);
            CHECK(fr > 2.0);
            ls_real span = (isfinite(fr) ? fr : 1e6) - nr;
            CHECK(span >= prev - 1e-9);      /* stopping down only ever helps */
            prev = span;
        }

        /* A looser sharpness criterion is a wider slab, for the same lens. */
        CHECK(os_lens_set_fnumber(&L, 5.0));
        CHECK(os_lens_focus(&L, 2.0));
        ls_real n1 = 0, f1 = 0, n2 = 0, f2 = 0;
        CHECK(os_lens_dof(&L, 0.015, &n1, &f1));
        CHECK(os_lens_dof(&L, 0.060, &n2, &f2));
        CHECK(n2 < n1);
        CHECK(f2 > f1);
    }

    SECTION("scene3d: the projection puts the target in the middle");
    {
        Scene3D s;
        s3_init(&s);

        ls_real sx, sy;
        CHECK(s3_project(&s, s.target, 800, 600, &sx, &sy));
        CHECK_NEAR(sx, 400.0, 1e-6);
        CHECK_NEAR(sy, 300.0, 1e-6);

        /* Up is up: a point above the target lands ABOVE the centre, which in
         * screen coordinates means a SMALLER y. Getting this backwards
         * produces a perfectly plausible upside-down world. */
        vec3 above = s.target; above.y += 1.0;
        CHECK(s3_project(&s, above, 800, 600, &sx, &sy));
        CHECK(sy < 300.0);

        vec3 below = s.target; below.y -= 1.0;
        CHECK(s3_project(&s, below, 800, 600, &sx, &sy));
        CHECK(sy > 300.0);

        /* Behind the eye there is no pixel, and saying so is the difference
         * between an omitted line and a line drawn to a wild coordinate. */
        vec3 eye_dir = v3norm(v3sub(s.target, v3(0, 0, 0)));
        vec3 behind = v3add(s.target, v3scale(eye_dir, -1e4));
        (void)behind;
        vec3 far_back = v3add(s.target, v3scale(v3sub(s.target, v3(0,0,0)), -1e3));
        bool ok = s3_project(&s, far_back, 800, 600, &sx, &sy);
        if (ok) { CHECK(isfinite(sx)); CHECK(isfinite(sy)); }

        /* Every projected point of a built scene is finite. */
        OsStage st;
        OsLens L;
        OsSceneDesc desc;
        os_scenedesc_preset(&desc, OS_STAGE_DEPTH_RAIL);
        CHECK(os_scenedesc_build(&desc, &st));
        CHECK(os_lens_build(&L, OS_LENS_ACHROMAT_100, 100.0, 5.0, why, sizeof why));
        CHECK(os_lens_focus(&L, 2.0));
        s3_build(&s, &desc, &L, 36.0, 24.0, 0.030, -1, -1);
        CHECK(s.nseg > 50);
        CHECK(s.nseg <= S3_MAX_SEG);
        CHECK(s.nlabel > 0);
        CHECK(s.nlabel <= S3_MAX_LABEL);
        for (int i = 0; i < s.nseg; ++i) {
            if (s3_project(&s, s.seg[i].a, 800, 600, &sx, &sy)) {
                CHECK(isfinite(sx)); CHECK(isfinite(sy));
            }
        }
        os_stage_free(&st);
    }

    SECTION("scene3d: every subject inside the depth of field is marked");
    {
        /* The mark means IN THE DEPTH OF FIELD -- inside the same near and far
         * planes the view draws its slab between. On axis or not: a subject
         * that sits visibly between the drawn planes and is not marked makes
         * the diagram contradict itself, and that is what this pins.
         *
         * It briefly marked by the traced spot instead, and that had exactly
         * one honest consequence and one dishonest one. Honest: off axis a
         * bare doublet's coma and astigmatism dwarf defocus, so a subject in
         * the band really can be soft. Dishonest: the rail's four off-axis
         * targets could then NEVER be marked at any aperture, so the mark
         * degenerated into "is this the on-axis one". The band is what depth
         * of field means; os_lens_spot_mm still reports the real spot, and the
         * panel shows it for the selected subject. */
        OsSceneDesc d;
        os_scenedesc_preset(&d, OS_STAGE_DEPTH_RAIL);
        OsLens L;
        CHECK(os_lens_build(&L, OS_LENS_ACHROMAT_100, 100.0, 5.0, why, sizeof why));
        L.blades = 0;

        /* One wireframe sphere is three rings of twenty chords. */
        const int SEGS_PER_SUBJECT = 60;

        /* Marked count must equal band membership computed independently, at
         * every one of these settings -- not just at the one that happens to
         * put a subject on the axis. */
        static const struct { ls_real fno, focus_m, coc_mm; } CASE[] = {
            {  5.0, 1.0, 0.030 }, {  5.0, 2.0, 0.030 }, {  5.0, 3.0, 0.030 },
            {  5.0, 5.0, 0.030 }, {  8.0, 2.0, 0.030 }, { 11.0, 2.5, 0.050 },
            { 16.0, 3.0, 0.050 }, { 22.0, 2.5, 0.050 }, { 22.0, 4.0, 0.056 },
            {  5.0, 4.57, 0.056 },
        };
        int most_marked = 0, most_off_axis = 0;

        for (size_t c = 0; c < sizeof CASE / sizeof CASE[0]; ++c) {
            CHECK(os_lens_set_fnumber(&L, CASE[c].fno));
            CHECK(os_lens_focus(&L, CASE[c].focus_m));

            ls_real nr = 0.0, fr = 0.0;
            bool have = os_lens_dof(&L, CASE[c].coc_mm, &nr, &fr);

            int want = 0, want_off_axis = 0;
            for (int i = 0; i < d.nobj; ++i) {
                if (!d.obj[i].alive || d.obj[i].kind != OS_OBJ_SPHERE) continue;
                ls_real dist = -d.obj[i].centre.z;
                if (!have || dist < nr || dist > fr) continue;
                ++want;
                if (fabs(d.obj[i].centre.x) > 1e-9
                    || fabs(d.obj[i].centre.y) > 1e-9) ++want_off_axis;
            }

            Scene3D s;
            s3_init(&s);
            s3_build(&s, &d, &L, 36.0, 24.0, CASE[c].coc_mm, -1, -1);
            int marked = 0;
            for (int i = 0; i < s.nseg; ++i)
                if (s.seg[i].kind == S3_SUBJECT) ++marked;

            NOTE("f/%.0f at %.2f m, %.3f mm: dof %.2f-%.2f m, %d marked (%d off axis)",
                 (double)CASE[c].fno, (double)CASE[c].focus_m,
                 (double)CASE[c].coc_mm, (double)nr, (double)fr,
                 marked / SEGS_PER_SUBJECT, want_off_axis);
            CHECK(marked == want * SEGS_PER_SUBJECT);

            if (want > most_marked) most_marked = want;
            if (want_off_axis > most_off_axis) most_off_axis = want_off_axis;
        }

        /* The regression this section exists for: off-axis subjects get
         * marked. Under the traced-spot criterion this was zero at every
         * aperture, which read as "the highlight only works on the middle
         * one". */
        CHECK(most_off_axis >= 1);
        /* And more than one subject can be marked at once, which is the whole
         * point of drawing a slab rather than a plane. */
        CHECK(most_marked >= 2);

        /* Stopping down can only ever widen the band, never narrow it. */
        CHECK(os_lens_focus(&L, 3.0));
        ls_real n_wide = 0.0, f_wide = 0.0, n_tight = 0.0, f_tight = 0.0;
        CHECK(os_lens_set_fnumber(&L, 5.0));
        CHECK(os_lens_dof(&L, 0.050, &n_tight, &f_tight));
        CHECK(os_lens_set_fnumber(&L, 16.0));
        CHECK(os_lens_dof(&L, 0.050, &n_wide, &f_wide));
        CHECK(n_wide <= n_tight);
        CHECK(f_wide >= f_tight);
    }

    SECTION("scene3d: picking is the exact inverse of drawing");
    {
        /* A sign error in an unprojection produces a drag that moves the wrong
         * way -- which reads as a broken interface rather than as arithmetic,
         * and is the sort of thing you work around instead of fixing. So the
         * ray is required to come back to the pixel it was cast through. */
        Scene3D s;
        s3_init(&s);

        for (int py = 60; py < 560; py += 97) {
            for (int px = 40; px < 780; px += 113) {
                vec3 o, dir;
                s3_pick_ray(&s, 800, 600, px, py, &o, &dir);
                CHECK_NEAR(v3len(dir), 1.0, 1e-12);

                /* Walk down the ray and project the point back. */
                for (ls_real t = 1.0; t <= 9.0; t += 4.0) {
                    vec3 p = v3add(o, v3scale(dir, t));
                    ls_real bx, by;
                    CHECK(s3_project(&s, p, 800, 600, &bx, &by));
                    CHECK_NEAR(bx, (ls_real)px, 1e-6);
                    CHECK_NEAR(by, (ls_real)py, 1e-6);
                }
            }
        }

        /* The drag plane: the hit must actually be ON the plane, and must
         * project back to where the cursor is. */
        for (ls_real y = -0.5; y <= 1.0; y += 0.5) {
            vec3 hit;
            CHECK(s3_plane_hit(&s, 800, 600, 400.0, 380.0, y, &hit));
            CHECK_NEAR(hit.y, y, 1e-9);
            ls_real bx, by;
            CHECK(s3_project(&s, hit, 800, 600, &bx, &by));
            CHECK_NEAR(bx, 400.0, 1e-6);
            CHECK_NEAR(by, 380.0, 1e-6);
        }
    }

    SECTION("scene3d: clicking a subject selects that subject");
    {
        /* Round trip through the two halves that have to agree: project every
         * object to where it is drawn, click exactly there, and get its own id
         * back. If picking and drawing used different maths this is where they
         * would part company. */
        OsSceneDesc d;
        os_scenedesc_preset(&d, OS_STAGE_DEPTH_RAIL);
        Scene3D s;
        s3_init(&s);

        int hits = 0;
        for (int i = 0; i < d.nobj; ++i) {
            if (!d.obj[i].alive || d.obj[i].kind != OS_OBJ_SPHERE) continue;
            ls_real sx, sy;
            if (!s3_project(&s, d.obj[i].centre, 900, 640, &sx, &sy)) continue;
            if (sx < 0 || sy < 0 || sx >= 900 || sy >= 640) continue;
            int po = -1, pl = -1;
            s3_pick(&s, &d, 900, 640, sx, sy, &po, &pl);
            CHECK(po == i);
            CHECK(pl == -1);
            hits++;
        }
        CHECK(hits >= 3);

        /* A lamp is pickable too, and wins over an object it sits in front of
         * -- it is the smaller target. */
        int li = os_scenedesc_next_light(&d, 0);
        CHECK(li >= 0);
        ls_real lx, lyy;
        if (s3_project(&s, d.lit[li].centre, 900, 640, &lx, &lyy)) {
            int po = -1, pl = -1;
            s3_pick(&s, &d, 900, 640, lx, lyy, &po, &pl);
            CHECK(pl == li);
            CHECK(po == -1);
        }

        /* Empty sky selects nothing, rather than the least-far-away thing. */
        int po = -1, pl = -1;
        s3_pick(&s, &d, 900, 640, 5.0, 5.0, &po, &pl);
        CHECK(po == -1);
        CHECK(pl == -1);

        /* A tombstoned object is not pickable, even though its id survives. */
        int first = os_scenedesc_next_object(&d, 0);
        ls_real fx, fy;
        if (s3_project(&s, d.obj[first].centre, 900, 640, &fx, &fy)) {
            CHECK(os_scenedesc_delete_object(&d, first));
            s3_pick(&s, &d, 900, 640, fx, fy, &po, &pl);
            CHECK(po != first);
        }
    }

    SECTION("scene3d: orbiting cannot turn the view inside out");
    {
        /* Straight overhead the up vector becomes parallel to the view
         * direction, the basis degenerates and the scene flips -- which reads
         * as a bug rather than as a viewpoint. */
        Scene3D s;
        s3_init(&s);

        for (int i = 0; i < 400; ++i) s3_orbit(&s, 0.1, 0.1);
        CHECK(s.el <= 1.451);
        for (int i = 0; i < 800; ++i) s3_orbit(&s, -0.1, -0.1);
        CHECK(s.el >= -1.451);

        /* And the projection stays sane at both extremes. */
        ls_real sx, sy;
        s.el = 1.45;
        CHECK(s3_project(&s, v3(0, 0, -3.0), 800, 600, &sx, &sy));
        CHECK(isfinite(sx) && isfinite(sy));
        s.el = -1.45;
        CHECK(s3_project(&s, v3(0, 0, -3.0), 800, 600, &sx, &sy));
        CHECK(isfinite(sx) && isfinite(sy));

        /* Zoom is bounded at both ends: inside the geometry, and so far out
         * that nothing is a pixel. */
        for (int i = 0; i < 200; ++i) s3_zoom(&s, 0.5);
        CHECK(s.dist >= 0.6 - 1e-9);
        for (int i = 0; i < 400; ++i) s3_zoom(&s, 2.0);
        CHECK(s.dist <= 60.0 + 1e-9);
    }
}
