/* test_inspect.c — the settings model, headlessly.
 *
 * The claim this suite defends is the one the whole panel rests on: there is
 * exactly ONE way to change a setting, and it clamps. A control that clamps
 * when dragged but not when typed produces a lens the tracer cannot build and
 * a blank window with no explanation -- and nothing about the panel looks
 * wrong while it happens. */
#include "test.h"
#include "tests.h"

#include "../viewer/inspect.h"

#include <math.h>
#include <string.h>

void os_test_inspect(void) {
    SECTION("inspect: defaults are inside their own bounds");
    {
        OsSettings s;
        os_settings_default(&s);

        Field f[OS_INSPECT_MAX];
        int n = os_inspect_fields(&s, NULL, 0, f, OS_INSPECT_MAX);
        CHECK(n > 10);
        CHECK(n <= OS_INSPECT_MAX);

        /* The list must FIT, not merely not overflow. os_inspect_fields stops
         * pushing when it runs out of room, which drops rows off the end
         * without a word -- so the worst case, a light selected with a lens
         * mounted, is checked against the cap with room to spare. */
        char why2[256];
        OsLens LL;
        CHECK(os_lens_build(&LL, OS_LENS_ACHROMAT_100, 100.0, 5.0, why2, sizeof why2));
        OsSettings big = s;
        big.sel_light = os_scenedesc_next_light(&big.scene, 0);
        int nbig = os_inspect_fields(&big, &LL, 0, f, OS_INSPECT_MAX);
        NOTE("longest field list: %d of %d rows", nbig, OS_INSPECT_MAX);
        CHECK(nbig < OS_INSPECT_MAX);

        for (int i = 0; i < n; ++i) {
            /* Every row is labelled, and every editable one has a real range.
             * A field with lo == hi is a control that cannot move, which looks
             * exactly like a control that is broken. */
            CHECK(f[i].label && f[i].label[0]);
            CHECK(f[i].unit != NULL);
            if (f[i].heading || f[i].readonly) continue;
            CHECK(f[i].hi > f[i].lo);
            CHECK(f[i].value >= f[i].lo - 1e-9);
            CHECK(f[i].value <= f[i].hi + 1e-9);
            /* And an enum's range must match its name table, or formatting
             * reads off the end of it. */
            if (f[i].is_enum) {
                CHECK(f[i].names != NULL);
                CHECK(f[i].nnames > 0);
                CHECK_NEAR(f[i].hi, f[i].nnames - 1, 1e-12);
            }
        }
    }

    SECTION("inspect: every editable field clamps, both ways");
    {
        /* Driven far past both ends of every range. This is the test that says
         * a typed number cannot reach somewhere a drag cannot.
         *
         * With something SELECTED, so the object and light rows are in the list
         * too -- those are the fields a drag writes, and they reach the
         * vendored light code that asserts on a bad value. */
        OsSettings s;
        os_settings_default(&s);
        s.sel_obj = os_scenedesc_next_object(&s.scene, 0);
        Field f[OS_INSPECT_MAX];
        int n = os_inspect_fields(&s, NULL, 0, f, OS_INSPECT_MAX);

        for (int i = 0; i < n; ++i) {
            if (f[i].heading || f[i].readonly) continue;
            FieldId id = f[i].id;

            os_inspect_set(&s, id, -1e9);
            Field g[OS_INSPECT_MAX];
            int m = os_inspect_fields(&s, NULL, 0, g, OS_INSPECT_MAX);
            CHECK(m == n);
            CHECK(g[i].value >= g[i].lo - 1e-9);

            os_inspect_set(&s, id, 1e9);
            m = os_inspect_fields(&s, NULL, 0, g, OS_INSPECT_MAX);
            CHECK(g[i].value <= g[i].hi + 1e-9);

            /* NaN must not get stored. A NaN focal length propagates into
             * every ray and the window simply goes black. */
            /* nan("") rather than NAN: the macro is a FLOAT, and promoting it
             * trips -Wdouble-promotion at the lower optimisation levels. */
            os_inspect_set(&s, id, nan(""));
            m = os_inspect_fields(&s, NULL, 0, g, OS_INSPECT_MAX);
            CHECK(!isnan(g[i].value));

            os_settings_default(&s);
            s.sel_obj = os_scenedesc_next_object(&s.scene, 0);
        }

        /* And again with a LIGHT selected, whose fields are the ones that can
         * abort a build rather than merely look wrong. */
        os_settings_default(&s);
        s.sel_obj = -1;
        s.sel_light = os_scenedesc_next_light(&s.scene, 0);
        CHECK(s.sel_light >= 0);
        n = os_inspect_fields(&s, NULL, 0, f, OS_INSPECT_MAX);
        for (int i = 0; i < n; ++i) {
            if (f[i].heading || f[i].readonly) continue;
            FieldId id = f[i].id;
            os_inspect_set(&s, id, -1e9);
            os_inspect_set(&s, id, 1e9);
            os_inspect_set(&s, id, nan(""));
            /* However it was driven, the description is still buildable. */
            OsStage st;
            CHECK(os_scenedesc_build(&s.scene, &st));
            os_stage_free(&st);
        }
    }

    SECTION("inspect: setting reports whether anything actually moved");
    {
        /* The return value is what decides whether to throw away a
         * half-converged render, so a no-op edit must say so. */
        OsSettings s;
        os_settings_default(&s);

        CHECK(!os_inspect_set(&s, FLD_FOCAL, s.focal_mm));
        CHECK(os_inspect_set(&s, FLD_FOCAL, 85.0));
        CHECK(!os_inspect_set(&s, FLD_FOCAL, 85.0));
        CHECK_NEAR(s.focal_mm, 85.0, 1e-12);

        /* Already at a limit: pushing further changes nothing and says so. */
        os_inspect_set(&s, FLD_FNO, 1e9);
        CHECK(!os_inspect_set(&s, FLD_FNO, 1e9));

        /* Headings and derived rows are not settable at all. */
        CHECK(!os_inspect_set(&s, FLD_H_LENS, 1.0));
        CHECK(!os_inspect_set(&s, FLD_D_EFL, 42.0));
        CHECK(!os_inspect_set(&s, FLD_NONE, 1.0));
    }

    SECTION("inspect: blade count skips the two values that are not shapes");
    {
        /* 0 is a circle and 3 upward are polygons. One and two blades are not
         * apertures, so dragging up from a circle lands on three rather than
         * stopping at a value the iris code would have to special-case. */
        OsSettings s;
        os_settings_default(&s);
        for (int v = 0; v <= 14; ++v) {
            os_inspect_set(&s, FLD_BLADES, v);
            CHECK(s.blades == 0 || s.blades >= 3);
        }
        os_inspect_set(&s, FLD_BLADES, 1);
        CHECK(s.blades == 3);
        os_inspect_set(&s, FLD_BLADES, 2);
        CHECK(s.blades == 3);
    }

    SECTION("inspect: scrubbing is multiplicative where the range spans decades");
    {
        OsSettings s;
        os_settings_default(&s);
        Field f[OS_INSPECT_MAX];
        int n = os_inspect_fields(&s, NULL, 0, f, OS_INSPECT_MAX);

        for (int i = 0; i < n; ++i) {
            if (f[i].heading || f[i].readonly || f[i].is_enum) continue;

            /* Dragging one way then back the same distance returns to where it
             * started, or a control drifts every time it is touched. */
            double v0 = f[i].value;
            double up = os_inspect_scrub(&f[i], v0, 40);
            Field g = f[i]; g.value = up;
            double back = os_inspect_scrub(&g, up, -40);
            if (up > f[i].lo * 1.001 && up < f[i].hi * 0.999)
                CHECK_NEAR(back, v0, 1e-6);

            /* And it never escapes the range whatever the drag. */
            CHECK(os_inspect_scrub(&f[i], v0, 100000) <= f[i].hi + 1e-9);
            CHECK(os_inspect_scrub(&f[i], v0, -100000) >= f[i].lo - 1e-9);
        }

        /* The point of the logarithmic rows: the same drag moves the same
         * NUMBER OF STOPS wherever it starts, which is how an aperture ring
         * behaves and what makes a 12-400 mm range usable at both ends. */
        for (int i = 0; i < n; ++i) {
            if (!f[i].logarithmic || f[i].readonly) continue;
            /* Both probes have to stay clear of the ends, or the comparison
             * is between a scrub and a clamp -- which is what a 1..256 sample
             * count does at 200. */
            if (20.0 < f[i].lo || 200.0 * 1.4 > f[i].hi) continue;
            double lo_ratio = os_inspect_scrub(&f[i], 20.0, 50) / 20.0;
            double hi_ratio = os_inspect_scrub(&f[i], 200.0, 50) / 200.0;
            CHECK_NEAR(lo_ratio, hi_ratio, 1e-9);
        }
    }

    SECTION("inspect: which characters a row will take as typed input");
    {
        /* The predicate the viewer's key handler and text handler BOTH consult,
         * so that a keystroke cannot be typing to one and a shortcut to the
         * other. It used to be two separate tests written in different words,
         * and they drifted: the key handler only knew a keystroke was typing
         * once a character had already been accepted, so the FIRST character of
         * every typed number also fired its hotkey. Typing 0.030 into SHARP IF
         * ran UI_RESET and rebuilt the scene. */
        OsSettings s;
        os_settings_default(&s);
        s.sel_obj = os_scenedesc_next_object(&s.scene, 0);

        Field f[OS_INSPECT_MAX];
        int n = os_inspect_fields(&s, NULL, 0, f, OS_INSPECT_MAX);
        CHECK(n > 0);

        int digits = 0, dots = 0, signs = 0, refused = 0;
        for (int i = 0; i < n; ++i) {
            const Field *r = &f[i];
            bool editable = !r->readonly && !r->heading && !r->is_enum;

            /* A digit is the one character every editable row takes, and no
             * other row takes anything at all. */
            for (char c = '0'; c <= '9'; ++c)
                CHECK(os_inspect_accepts_char(r, c) == editable);
            if (editable) digits++; else refused++;

            /* A decimal point means nothing on an integer count. */
            CHECK(os_inspect_accepts_char(r, '.') == (editable && !r->integral));
            if (editable && !r->integral) dots++;

            /* Nor does a sign, on a row that cannot go below zero. */
            CHECK(os_inspect_accepts_char(r, '-') == (editable && r->lo < 0.0));
            if (editable && r->lo < 0.0) signs++;

            /* Nothing else, ever -- a letter must stay a shortcut. */
            CHECK(!os_inspect_accepts_char(r, 'b'));
            CHECK(!os_inspect_accepts_char(r, 'z'));
            CHECK(!os_inspect_accepts_char(r, '='));
            CHECK(!os_inspect_accepts_char(r, ' '));
            CHECK(!os_inspect_accepts_char(r, '\0'));
        }
        NOTE("%d rows take digits, %d of those take '.', %d take '-'; "
             "%d rows take nothing", digits, dots, signs, refused);
        /* All three classes have to exist, or the rules above are being
         * checked against a panel that cannot exercise them. */
        CHECK(digits > 0);
        CHECK(refused > 0);
        CHECK(dots > 0 && dots < digits);      /* some integer rows           */
        CHECK(signs > 0 && signs < digits);    /* some rows can go negative   */

        /* A null field is refused rather than dereferenced: the caller resolves
         * a selection index, and "nothing is selected" has to be answerable. */
        CHECK(!os_inspect_accepts_char(NULL, '5'));
    }

    SECTION("inspect: a logarithmic row can always be dragged back up");
    {
        /* THE trap in a multiplicative control: zero has no logarithm, and
         * anything times zero is zero. A lamp's FLUX and the sky's AMBIENT are
         * both declared with a floor of 0, so dragging one all the way left
         * used to land on exactly 0 and then STAY there for every drag after
         * -- the row was dead until someone clicked it and typed a number.
         *
         * Checked on the fields the panel really builds, so a new row declared
         * the same way is covered the day it is added. */
        OsSettings s;
        os_settings_default(&s);
        /* The dome's rows only exist while the dome is the light source, and
         * a lamp's only while one is selected. */
        s.scene.light_mode = OS_LIGHT_AMBIENT;
        s.sel_light = os_scenedesc_next_light(&s.scene, 0);

        Field f[OS_INSPECT_MAX];
        int n = os_inspect_fields(&s, NULL, 0, f, OS_INSPECT_MAX);

        int checked = 0;
        for (int i = 0; i < n; ++i) {
            if (!f[i].logarithmic || f[i].readonly || f[i].is_enum) continue;
            if (f[i].lo > 0.0) continue;               /* has its own floor */
            checked++;

            /* All the way to the bottom, however far anyone drags... */
            double bottom = os_inspect_scrub(&f[i], f[i].value, -100000);
            CHECK(bottom >= 0.0);
            /* ...and one pixel back up moves it again. */
            Field g = f[i]; g.value = bottom;
            CHECK(os_inspect_scrub(&g, bottom, 1) > bottom);
            /* A real drag gets somewhere useful rather than crawling out of a
             * denormal: a stop is about 115 px, so 400 px is several. */
            CHECK(os_inspect_scrub(&g, bottom, 400) > bottom * 4.0);
        }
        NOTE("%d zero-floored logarithmic rows, all recoverable", checked);
        CHECK(checked >= 2);            /* the lamp's flux and the sky's lux */
    }

    SECTION("inspect: a continuous row is not scrubbed like an integer one");
    {
        /* The coarse whole-unit step exists for BLADES, which would otherwise
         * need a 200-pixel drag to move by one. It used to be selected by the
         * row's SPAN -- anything narrower than 32 -- which caught every
         * continuous 0-to-1 row as well: the blade curvature and the three
         * object colour channels crossed their entire range in twelve pixels,
         * so an object's colour could not be adjusted at all. */
        OsSettings s;
        os_settings_default(&s);
        s.sel_obj = os_scenedesc_next_object(&s.scene, 0);

        Field f[OS_INSPECT_MAX];
        int n = os_inspect_fields(&s, NULL, 0, f, OS_INSPECT_MAX);

        int narrow = 0, coarse = 0;
        for (int i = 0; i < n; ++i) {
            if (f[i].heading || f[i].readonly || f[i].is_enum) continue;
            if (f[i].logarithmic) continue;
            if (f[i].hi - f[i].lo > 32.0) continue;
            narrow++;

            /* Ten pixels must not cross a whole range that is not an integer
             * count -- which is the difference the old rule could not see. */
            double moved = fabs(os_inspect_scrub(&f[i], f[i].value, 10)
                                - f[i].value);
            if (f[i].integral) { coarse++; CHECK(moved >= 0.5); }
            else               CHECK(moved < (f[i].hi - f[i].lo) * 0.25);
        }
        NOTE("%d narrow rows, %d of them integer-valued", narrow, coarse);
        CHECK(narrow > coarse);       /* there ARE continuous narrow rows */
        CHECK(coarse >= 1);           /* and BLADES is still steppable    */
    }

    SECTION("inspect: only the photograph's settings restart a render");
    {
        /* Turning the ray fan off must not throw away a converged image, and
         * changing the aperture must. This comparison is the single place that
         * decision is made, so it is the single place it can be got wrong. */
        OsSettings a, b;
        os_settings_default(&a);
        b = a;
        CHECK(!os_settings_image_differs(&a, &b));

        b = a; b.show_rays = !a.show_rays;
        CHECK(!os_settings_image_differs(&a, &b));
        b = a; b.show_grid = !a.show_grid;
        CHECK(!os_settings_image_differs(&a, &b));
        b = a; b.chromatic = !a.chromatic;
        CHECK(!os_settings_image_differs(&a, &b));
        b = a; b.view = OS_VIEW_IMAGE;
        CHECK(!os_settings_image_differs(&a, &b));
        /* Exposure is a view gain applied at tone-map time, not a change to
         * the film. */
        b = a; b.exposure = a.exposure * 4.0;
        CHECK(!os_settings_image_differs(&a, &b));

        b = a; b.fno = 11.0;         CHECK(os_settings_image_differs(&a, &b));
        b = a; b.focal_mm = 85.0;    CHECK(os_settings_image_differs(&a, &b));
        b = a; b.focus_m = 5.0;      CHECK(os_settings_image_differs(&a, &b));
        b = a; b.lens = OS_LENS_SINGLET_100;  CHECK(os_settings_image_differs(&a, &b));
        b = a; b.stage = OS_STAGE_BOKEH;      CHECK(os_settings_image_differs(&a, &b));
        b = a; b.blades = 6;         CHECK(os_settings_image_differs(&a, &b));
        b = a; b.curvature = 0.5;    CHECK(os_settings_image_differs(&a, &b));
        b = a; b.rot_deg = 15.0;     CHECK(os_settings_image_differs(&a, &b));
        b = a; b.sensor_w_mm = 24.0; CHECK(os_settings_image_differs(&a, &b));
        b = a; b.res_w = 640;        CHECK(os_settings_image_differs(&a, &b));
        b = a; b.spp = 16;           CHECK(os_settings_image_differs(&a, &b));
        b = a; b.depth = 8;          CHECK(os_settings_image_differs(&a, &b));

        /* Selecting is not an edit: it must not throw away a converged
         * render. Moving the thing you selected obviously is. */
        b = a; b.sel_obj = 2;        CHECK(!os_settings_image_differs(&a, &b));
        b = a; b.sel_light = 0;      CHECK(!os_settings_image_differs(&a, &b));

        b = a; b.scene.obj[0].centre.z = -4.0;
        CHECK(os_settings_image_differs(&a, &b));
        b = a; b.scene.obj[0].rgb[0] = 0.1;
        CHECK(os_settings_image_differs(&a, &b));
        b = a; b.scene.lit[0].flux_lm *= 2.0;
        CHECK(os_settings_image_differs(&a, &b));
        b = a; b.scene.lit[0].cct_k = 3000.0;
        CHECK(os_settings_image_differs(&a, &b));
        b = a; CHECK(os_scenedesc_delete_object(&b.scene, 0));
        CHECK(os_settings_image_differs(&a, &b));
        b = a; CHECK(os_scenedesc_add_object(&b.scene, OS_OBJ_SPHERE) >= 0);
        CHECK(os_settings_image_differs(&a, &b));
    }

    SECTION("inspect: every field formats to something readable");
    {
        OsSettings s;
        os_settings_default(&s);
        char why[256];
        OsLens L;
        bool have = os_lens_build(&L, OS_LENS_ACHROMAT_100, 100.0, 5.0,
                                  why, sizeof why);
        CHECK(have);

        Field f[OS_INSPECT_MAX];
        int n = os_inspect_fields(&s, &L, 4096, f, OS_INSPECT_MAX);
        for (int i = 0; i < n; ++i) {
            char buf[64];
            os_inspect_format(&f[i], buf, sizeof buf);
            if (f[i].heading) { CHECK(buf[0] == '\0'); continue; }
            CHECK(buf[0] != '\0');
            CHECK(strstr(buf, "nan") == NULL);
            CHECK(strstr(buf, "inf") == NULL);
        }

        /* The rows that read as words rather than numbers. */
        Field g;
        memset(&g, 0, sizeof g);
        char buf[64];
        g.id = FLD_FOCUS; g.unit = "M"; g.value = 1000.0;
        os_inspect_format(&g, buf, sizeof buf);
        CHECK(strcmp(buf, "INFINITY") == 0);
        g.id = FLD_BLADES; g.unit = ""; g.value = 0.0;
        os_inspect_format(&g, buf, sizeof buf);
        CHECK(strcmp(buf, "CIRCLE") == 0);
        g.id = FLD_FNO; g.value = 5.6;
        os_inspect_format(&g, buf, sizeof buf);
        CHECK(strcmp(buf, "F/5.6") == 0);

        /* Whole-number rows print as whole numbers: "6.00 BLADES" reads as a
         * quantity that could be fractional, and it cannot be. */
        memset(&g, 0, sizeof g);
        g.id = FLD_DEPTH; g.unit = ""; g.value = 5.0; g.integral = true;
        os_inspect_format(&g, buf, sizeof buf);
        CHECK(strstr(buf, ".") == NULL);
        for (int i = 0; i < n; ++i)
            if (f[i].integral) {
                os_inspect_format(&f[i], buf, sizeof buf);
                CHECK(strstr(buf, ".") == NULL);
            }
    }

    SECTION("inspect: the derived rows say what the lens is doing");
    {
        /* The panel's whole job on the right-hand side: swap the singlet for
         * the achromat and the colour error must fall by more than an order of
         * magnitude, with nothing in the UI special-casing it. */
        OsSettings s;
        os_settings_default(&s);
        char why[256];
        double err[2];

        const OsPrescriptionId ids[2] = { OS_LENS_SINGLET_100, OS_LENS_ACHROMAT_100 };
        for (int k = 0; k < 2; ++k) {
            OsLens L;
            CHECK(os_lens_build(&L, ids[k], 100.0, 8.0, why, sizeof why));
            Field f[OS_INSPECT_MAX];
            int n = os_inspect_fields(&s, &L, 0, f, OS_INSPECT_MAX);
            err[k] = 0.0;
            for (int i = 0; i < n; ++i)
                if (f[i].id == FLD_D_COLOUR) err[k] = fabs(f[i].value);
            CHECK(err[k] > 0.0);
        }
        NOTE("panel colour error: singlet %.4f %%, achromat %.4f %%",
             err[0], err[1]);
        CHECK(err[1] < 0.1 * err[0]);
    }
}
