/* test_ui.c — the toolbar's rules and the cross-section's geometry, with no
 * window anywhere.
 *
 * These suites exist because ui.c and lensplot.c are deliberately SDL-free.
 * The enable rules are the kind of thing that rots quietly -- a button that
 * can never be pressed, a hotkey that reaches a command its button refuses --
 * and none of it shows up in a screenshot. */
#include "test.h"
#include "tests.h"

#include "../viewer/ui.h"
#include "../viewer/font.h"
#include "../viewer/lensplot.h"
#include "../viewer/inspect.h"

#include <math.h>
#include <string.h>

static UiState nominal(void) {
    UiState s;
    memset(&s, 0, sizeof s);
    s.lens_count = 3;
    s.focal_mm = 100.0; s.focal_min_mm = 12.0; s.focal_max_mm = 400.0;
    s.fno = 5.0;        s.fno_min = 1.0;       s.fno_max = 45.0;
    s.focus_m = 2.0;    s.focus_min_m = 0.15;  s.focus_max_m = 1e6;
    s.showing_rays = true;
    /* The diagram overlays only apply to the lens view, and view 0 is now the
     * SCENE view -- so a nominal state has to say which view it means rather
     * than relying on the zero. */
    s.view = OS_VIEW_LENS;
    return s;
}

void os_test_ui(void) {
    SECTION("ui: the toolbar is well formed");
    {
        Toolbar t;
        ui_init(&t, 760);
        CHECK(t.count > 0);

        for (int i = 0; i < t.count; ++i) {
            const UiButton *b = &t.buttons[i];
            /* Every button has a label, a hotkey and hover text. The tip is
             * also the message shown when the command is refused, so a button
             * without one fails silently AND wordlessly. */
            CHECK(b->label && b->label[0]);
            CHECK(b->hint  && b->hint[0]);
            CHECK(b->tip   && strlen(b->tip) > 20);

            /* It fits the strip and the label clears its hotkey hint. */
            CHECK(b->rect.w > 0 && b->rect.h > 0);
            CHECK(b->rect.x >= 0);
            CHECK(b->rect.y + b->rect.h <= 760);
            int used = font_text_width(b->label, 1) + font_text_width(b->hint, 1) + 16;
            CHECK(used <= b->rect.w);
        }

        /* No two buttons overlap. */
        for (int i = 0; i < t.count; ++i)
            for (int j = i + 1; j < t.count; ++j) {
                const UiRect *p = &t.buttons[i].rect, *q = &t.buttons[j].rect;
                bool apart = p->y + p->h <= q->y || q->y + q->h <= p->y
                          || p->x + p->w <= q->x || q->x + q->w <= p->x;
                CHECK(apart);
            }

        /* Hit-testing agrees with the rectangles it laid out. */
        for (int i = 0; i < t.count; ++i) {
            const UiRect *q = &t.buttons[i].rect;
            CHECK(ui_hit(&t, q->x + q->w / 2, q->y + q->h / 2) == i);
        }
        CHECK(ui_hit(&t, -5, 5) == -1);
        CHECK(ui_hit(&t, 5, 100000) == -1);
    }

    SECTION("ui: it squeezes to fit a short window instead of overflowing");
    {
        /* The buttons used to run off the bottom of a laptop screen, which
         * reads as a missing feature rather than a layout problem. */
        for (int h = 300; h <= 1200; h += 37) {
            Toolbar t;
            ui_init(&t, h);
            for (int i = 0; i < t.count; ++i) {
                CHECK(t.buttons[i].rect.h >= UI_BUTTON_MIN_H);
                if (h >= 460) CHECK(t.buttons[i].rect.y + t.buttons[i].rect.h <= h);
            }
        }
    }

    SECTION("ui: every hotkey reaches a real button, and vice versa");
    {
        Toolbar t;
        ui_init(&t, 760);

        /* Every bound key must name an action the toolbar actually has. A key
         * that reaches nothing is a command with no way to discover it. */
        for (int c = 32; c < 127; ++c) {
            UiAction a = ui_action_for_key((char)c);
            if (a == UI_NONE) continue;
            bool found = false;
            for (int i = 0; i < t.count; ++i)
                if (t.buttons[i].action == a) found = true;
            CHECK(found);
        }

        /* And every button's hint names a key that maps back to it, so the
         * label on screen and the key under the finger cannot disagree.
         *
         * A hint longer than one character is a NAMED key -- "TAB" -- and is
         * looked up as such rather than by its first letter, which would test
         * that 'T' does something it never claimed to. */
        for (int i = 0; i < t.count; ++i) {
            const UiButton *b = &t.buttons[i];
            char k;
            if (strcmp(b->hint, "TAB") == 0) {
                k = '\t';
            } else {
                CHECK(strlen(b->hint) == 1);   /* no unnamed multi-char hints */
                k = b->hint[0];
                if (k >= 'A' && k <= 'Z') k = (char)(k - 'A' + 'a');
            }
            CHECK(ui_action_for_key(k) == b->action);
        }
    }

    SECTION("ui: controls disable themselves at their limits");
    {
        Toolbar t;
        ui_init(&t, 760);

        UiState s = nominal();
        ui_apply_state(&t, s);
        CHECK(ui_action_enabled(&t, UI_OPEN_UP));
        CHECK(ui_action_enabled(&t, UI_STOP_DOWN));
        CHECK(ui_action_enabled(&t, UI_FOCUS_NEAR));
        CHECK(ui_action_enabled(&t, UI_FOCUS_FAR));

        /* At its limit a control greys out rather than accepting a click and
         * doing nothing: a control that does nothing reads as a broken
         * program, one that greys out reads as a limit. */
        s = nominal(); s.fno = s.fno_min;
        ui_apply_state(&t, s);
        CHECK(!ui_action_enabled(&t, UI_OPEN_UP));
        CHECK(ui_action_enabled(&t, UI_STOP_DOWN));

        /* Marking where rays cross the axis is meaningless with the ray fan
         * switched off. */
        s = nominal(); s.showing_rays = false;
        ui_apply_state(&t, s);
        CHECK(!ui_action_enabled(&t, UI_SPOT));
        s.showing_rays = true;
        ui_apply_state(&t, s);
        CHECK(ui_action_enabled(&t, UI_SPOT));

        /* The diagram overlays mean nothing while the canvas is showing a
         * photograph, so they grey out there rather than silently toggling
         * something that is not on screen. */
        s = nominal(); s.view = OS_VIEW_IMAGE;
        ui_apply_state(&t, s);
        CHECK(!ui_action_enabled(&t, UI_RAYS));
        CHECK(!ui_action_enabled(&t, UI_COLOUR));
        CHECK(!ui_action_enabled(&t, UI_SPOT));
        CHECK(!ui_action_enabled(&t, UI_GRID));

        /* And there is nothing to save until a render has actually run. */
        s = nominal(); s.view = OS_VIEW_IMAGE; s.rendering = false;
        ui_apply_state(&t, s);
        CHECK(!ui_action_enabled(&t, UI_SAVE));
        s.rendering = true;
        ui_apply_state(&t, s);
        CHECK(ui_action_enabled(&t, UI_SAVE));

        /* Toggles report their own state through `active`. */
        s = nominal(); s.chromatic = true; s.showing_grid = true;
        ui_apply_state(&t, s);
        for (int i = 0; i < t.count; ++i) {
            if (t.buttons[i].action == UI_COLOUR) CHECK(t.buttons[i].active);
            if (t.buttons[i].action == UI_GRID)   CHECK(t.buttons[i].active);
            if (t.buttons[i].action == UI_RAYS)   CHECK(t.buttons[i].active);
        }

        /* The two view buttons are mutually exclusive and always agree with
         * what the canvas is actually showing. */
        s = nominal(); s.view = OS_VIEW_LENS;
        ui_apply_state(&t, s);
        for (int i = 0; i < t.count; ++i) {
            if (t.buttons[i].action == UI_VIEW_LENS)  CHECK(t.buttons[i].active);
            if (t.buttons[i].action == UI_VIEW_IMAGE) CHECK(!t.buttons[i].active);
        }
        s.view = OS_VIEW_IMAGE;
        ui_apply_state(&t, s);
        for (int i = 0; i < t.count; ++i) {
            if (t.buttons[i].action == UI_VIEW_LENS)  CHECK(!t.buttons[i].active);
            if (t.buttons[i].action == UI_VIEW_IMAGE) CHECK(t.buttons[i].active);
        }
    }

    SECTION("lensplot: the diagram is the trace, not a schematic");
    {
        char why[256];
        OsLens L;
        CHECK(os_lens_build(&L, OS_LENS_ACHROMAT_100, 100.0, 8.0, why, sizeof why));

        LensPlot lp;
        lp_build(&lp, &L, 15, false, HUGE_VAL);

        CHECK(lp.nprofiles == L.nsurf);
        CHECK(lp.nrays == 15);
        CHECK(lp.x_max > 0.0);
        CHECK(lp.z_max > lp.z_min);

        /* Each surface profile spans its own clear aperture, symmetrically. */
        for (int i = 0; i < lp.nprofiles; ++i) {
            const LpProfile *p = &lp.profile[i];
            CHECK(p->n == LP_PROFILE_PTS);
            CHECK_NEAR(p->x[0], -p->x[p->n - 1], 1e-12);
            /* The sag is zero on axis: the middle sample sits at the vertex. */
            CHECK_NEAR(p->z[p->n / 2], os_lens_vertex_z(&L, i), 1e-9);
        }

        /* Every ray that got through crosses the axis somewhere sensible, and
         * the unblocked ones cluster near the paraxial focus. This is the
         * check that the drawn picture agrees with the optics. */
        int through = 0;
        for (int i = 0; i < lp.nrays; ++i) {
            const LpRay *r = &lp.ray[i];
            CHECK(r->n >= 2);
            if (r->blocked) continue;
            through++;
            /* The exactly-axial ray is skipped: it travels ALONG the axis, so
             * it has no crossing to report and lp_build hands back HUGE_VAL.
             * That is the honest answer rather than a fabricated focus, and
             * the drawing code omits it for the same reason. With an odd ray
             * count the fan always contains one. */
            if (fabs(r->x[0]) < 1e-9) { CHECK(!isfinite(r->cross_z)); continue; }
            CHECK(isfinite(r->cross_z));
            /* Within a millimetre of the paraxial back focus: an achromat at
             * f/8 has little spherical aberration left. */
            CHECK(fabs(r->cross_z - L.bfd_mm) < 1.0);
        }
        CHECK(through > 0);

        /* The fan deliberately overshoots the pupil so the clipped rays are
         * drawn being clipped -- vignetting you can see. */
        CHECK(through < lp.nrays);

        /* Chromatic mode traces three wavelengths per fan line, and they are
         * the three Fraunhofer lines. */
        lp_build(&lp, &L, 9, true, HUGE_VAL);
        CHECK(lp.nrays == 27);
        int nF = 0, nd = 0, nC = 0;
        for (int i = 0; i < lp.nrays; ++i) {
            if (lp.ray[i].lambda_nm == OS_LINE_F) nF++;
            if (lp.ray[i].lambda_nm == OS_LINE_D) nd++;
            if (lp.ray[i].lambda_nm == OS_LINE_C) nC++;
        }
        CHECK(nF == 9); CHECK(nd == 9); CHECK(nC == 9);
    }

    SECTION("lensplot: the singlet's colours separate, the achromat's do not");
    {
        /* The same claim the paraxial and trace suites make, now made about
         * the numbers the DIAGRAM will draw -- so the picture cannot show a
         * corrected singlet or an uncorrected achromat. */
        char why[256];
        OsLens s, a;
        CHECK(os_lens_build(&s, OS_LENS_SINGLET_100,  100.0, 8.0, why, sizeof why));
        CHECK(os_lens_build(&a, OS_LENS_ACHROMAT_100, 100.0, 8.0, why, sizeof why));

        ls_real spread[2];
        OsLens *lens[2] = { &s, &a };
        for (int k = 0; k < 2; ++k) {
            LensPlot lp;
            lp_build(&lp, lens[k], 5, true, HUGE_VAL);
            ls_real lo = HUGE_VAL, hi = -HUGE_VAL;
            for (int i = 0; i < lp.nrays; ++i) {
                const LpRay *r = &lp.ray[i];
                if (r->blocked || !isfinite(r->cross_z)) continue;
                if (r->cross_z < lo) lo = r->cross_z;
                if (r->cross_z > hi) hi = r->cross_z;
            }
            spread[k] = hi - lo;
        }
        NOTE("axis-crossing spread over F/d/C: singlet %.4f mm, achromat %.4f mm",
             spread[0], spread[1]);
        CHECK(spread[0] > 1.0);
        CHECK(spread[1] < 0.5 * spread[0]);
    }
}
