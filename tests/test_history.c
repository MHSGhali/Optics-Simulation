/* test_history.c — undo and redo.
 *
 * WHY THIS IS TESTED HEADLESSLY AND NOT BY CLICKING
 *   Undo is a state machine, and the ways it goes wrong are the ways state
 *   machines go wrong: a redo that survives an edit it should not have, a
 *   stack that stops recording when it fills, a step that half-restores. None
 *   of those look like anything on screen until the one time they matter, and
 *   by then the state they should have restored is gone.
 *
 *   So the module is free of SDL and the discipline is asserted directly.
 *
 * THE PROPERTY THAT MATTERS MOST
 *   N edits then N undos returns to EXACTLY the starting document -- compared
 *   with memcmp, not field by field, because a field-by-field check can only
 *   find what its author remembered to list.
 */
#include "test.h"
#include "tests.h"

#include "../viewer/history.h"

#include <math.h>
#include <string.h>

/* The document half of two settings, compared the only way that cannot be
 * incomplete. The viewport half is zeroed first so it cannot contribute. */
static bool same_doc(const OsSettings *a, const OsSettings *b) {
    OsSettings x = *a, y = *b;
    x.view = y.view = 0;
    x.show_rays = y.show_rays = false;
    x.show_spot = y.show_spot = false;
    x.show_grid = y.show_grid = false;
    x.chromatic = y.chromatic = false;
    return memcmp(&x, &y, sizeof x) == 0;
}

/* A deterministic edit, so a failure reproduces. Each `i` touches a different
 * kind of setting: a scalar, an enum, the arrangement, the selection. */
static void edit(OsSettings *s, int i) {
    switch (i % 5) {
        case 0: os_inspect_set(s, FLD_FNO,   2.0 + (double)(i % 9)); break;
        case 1: os_inspect_set(s, FLD_FOCUS, 0.5 + 0.25 * (double)i); break;
        case 2: os_inspect_set(s, FLD_LENS,  (double)(i % OS_LENS_COUNT)); break;
        case 3: {
            int id = os_scenedesc_add_object(&s->scene, OS_OBJ_SPHERE);
            if (id >= 0) { s->sel_obj = id; s->scene.obj[id].centre.z = -(double)i; }
            break;
        }
        default: os_inspect_set(s, FLD_EXPOSURE, 10.0 * (double)(i % 7 + 1)); break;
    }
}

void os_test_history(void) {
    SECTION("history: an empty history offers nothing");
    {
        OsHistory h;
        CHECK(os_history_init(&h));
        CHECK(!os_history_can_undo(&h));
        CHECK(!os_history_can_redo(&h));

        OsSettings s;
        os_settings_default(&s);
        OsSettings before = s;
        CHECK(!os_history_undo(&h, &s));
        CHECK(!os_history_redo(&h, &s));
        /* A refused step must not have half-happened. */
        CHECK(memcmp(&s, &before, sizeof s) == 0);
        os_history_free(&h);
    }

    SECTION("history: a gesture that changed nothing records nothing");
    {
        /* The classic bug. A click that missed, or a scrub of zero pixels,
         * still closes a gesture -- and if that recorded a step, the first
         * press of undo would appear to do nothing at all, which reads as the
         * whole feature being broken. */
        OsHistory h;
        CHECK(os_history_init(&h));
        OsSettings s;
        os_settings_default(&s);

        for (int i = 0; i < 50; ++i)
            CHECK(!os_history_record(&h, &s, &s));
        CHECK(h.nundo == 0);
        CHECK(!os_history_can_undo(&h));

        /* Nor does moving the VIEWPORT. Which view is on screen is not an
         * edit; recording it would mean undo spending its steps on things
         * nobody asked to take back. */
        OsSettings v = s;
        v.view = OS_VIEW_IMAGE;
        v.show_rays = !v.show_rays;
        v.chromatic = !v.chromatic;
        CHECK(!os_history_record(&h, &s, &v));
        CHECK(h.nundo == 0);
        os_history_free(&h);
    }

    SECTION("history: one step back, and forward again");
    {
        OsHistory h;
        CHECK(os_history_init(&h));
        OsSettings s;
        os_settings_default(&s);
        OsSettings origin = s;

        OsSettings before = s;
        CHECK(os_inspect_set(&s, FLD_FNO, 11.0));
        CHECK(os_history_record(&h, &before, &s));
        CHECK(os_history_can_undo(&h));
        CHECK(!os_history_can_redo(&h));

        CHECK(os_history_undo(&h, &s));
        CHECK_NEAR(s.fno, origin.fno, 1e-12);
        CHECK(same_doc(&s, &origin));
        CHECK(!os_history_can_undo(&h));
        CHECK(os_history_can_redo(&h));

        CHECK(os_history_redo(&h, &s));
        CHECK_NEAR(s.fno, 11.0, 1e-12);
        CHECK(os_history_can_undo(&h));
        CHECK(!os_history_can_redo(&h));
        os_history_free(&h);
    }

    SECTION("history: undo restores the document and leaves the viewport");
    {
        /* Undoing an aperture change while you are looking at the lens view
         * must not throw you into the image view. That is obeying the letter
         * of the word and not its point. */
        OsHistory h;
        CHECK(os_history_init(&h));
        OsSettings s;
        os_settings_default(&s);
        s.view = OS_VIEW_LENS;
        s.show_rays = true;
        s.chromatic = false;

        OsSettings before = s;
        CHECK(os_inspect_set(&s, FLD_FOCAL, 200.0));
        CHECK(os_history_record(&h, &before, &s));

        /* Move the viewport AFTER the edit, the way a person would. */
        s.view = OS_VIEW_IMAGE;
        s.chromatic = true;

        CHECK(os_history_undo(&h, &s));
        CHECK_NEAR(s.focal_mm, before.focal_mm, 1e-12);   /* document: back */
        CHECK(s.view == OS_VIEW_IMAGE);                   /* viewport: kept */
        CHECK(s.chromatic == true);
        CHECK(s.show_rays == true);
        os_history_free(&h);
    }

    SECTION("history: selection travels with the step, so an undone delete reselects");
    {
        /* Selection is not an edit on its own, but a delete changes it -- and
         * an undo that brought the object back without reselecting it would
         * leave the panel editing nothing and the scene view marking nothing,
         * which reads as the undo having only half worked. */
        OsHistory h;
        CHECK(os_history_init(&h));
        OsSettings s;
        os_settings_default(&s);

        int id = os_scenedesc_next_object(&s.scene, 0);
        CHECK(id >= 0);
        s.sel_obj = id;
        s.sel_light = -1;

        OsSettings before = s;
        CHECK(os_scenedesc_delete_object(&s.scene, id));
        s.sel_obj = -1;
        CHECK(os_history_record(&h, &before, &s));
        CHECK(!s.scene.obj[id].alive);

        CHECK(os_history_undo(&h, &s));
        CHECK(s.scene.obj[id].alive);
        CHECK(s.sel_obj == id);
        /* And the id itself is unchanged, because the description tombstones
         * rather than compacting -- see scenedesc.h. */
        CHECK(s.scene.nobj == before.scene.nobj);
        os_history_free(&h);
    }

    SECTION("history: a new edit after an undo abandons the redo trail");
    {
        OsHistory h;
        CHECK(os_history_init(&h));
        OsSettings s;
        os_settings_default(&s);

        OsSettings b1 = s; CHECK(os_inspect_set(&s, FLD_FNO, 8.0));
        CHECK(os_history_record(&h, &b1, &s));
        OsSettings b2 = s; CHECK(os_inspect_set(&s, FLD_FNO, 16.0));
        CHECK(os_history_record(&h, &b2, &s));

        CHECK(os_history_undo(&h, &s));
        CHECK(os_history_can_redo(&h));

        /* Branch away. The redo trail led to a state the edits since cannot
         * account for, so it goes. */
        OsSettings b3 = s; CHECK(os_inspect_set(&s, FLD_FOCUS, 7.0));
        CHECK(os_history_record(&h, &b3, &s));
        CHECK(!os_history_can_redo(&h));
        CHECK(h.nredo == 0);
        os_history_free(&h);
    }

    SECTION("history: N edits then N undos is exactly where it started");
    {
        /* The load-bearing one, and compared with memcmp so it cannot be
         * incomplete the way a list of field checks always can. */
        OsHistory h;
        CHECK(os_history_init(&h));
        OsSettings s;
        os_settings_default(&s);
        OsSettings origin = s;

        int steps = 0;
        for (int i = 0; i < 40; ++i) {
            OsSettings before = s;
            edit(&s, i);
            if (os_history_record(&h, &before, &s)) ++steps;
        }
        NOTE("40 edits recorded %d steps", steps);
        CHECK(steps > 30);           /* a few are no-ops by construction */

        for (int i = 0; i < steps; ++i) CHECK(os_history_undo(&h, &s));
        CHECK(same_doc(&s, &origin));
        CHECK(!os_history_can_undo(&h));

        /* And forward again, to exactly where it left off. */
        OsSettings end_state;
        for (int i = 0; i < steps; ++i) CHECK(os_history_redo(&h, &s));
        end_state = s;
        CHECK(!os_history_can_redo(&h));

        /* Back and forth repeatedly must not drift. A step that restored
         * almost everything would show up here as a slow leak and nowhere
         * else. */
        for (int round = 0; round < 5; ++round) {
            for (int i = 0; i < steps; ++i) CHECK(os_history_undo(&h, &s));
            CHECK(same_doc(&s, &origin));
            for (int i = 0; i < steps; ++i) CHECK(os_history_redo(&h, &s));
            CHECK(same_doc(&s, &end_state));
        }
        os_history_free(&h);
    }

    SECTION("history: at the depth limit the OLDEST step is dropped");
    {
        /* A stack that refused new steps when full would silently stop
         * recording, which is the one failure an undo feature must not have:
         * everything you do from then on is unrepeatable and nothing says so.
         * Dropping the oldest loses the least useful step instead. */
        OsHistory h;
        CHECK(os_history_init(&h));
        OsSettings s;
        os_settings_default(&s);

        const int EXTRA = 20;
        for (int i = 0; i < OS_HISTORY_DEPTH + EXTRA; ++i) {
            OsSettings before = s;
            /* Each step is a distinct, recognisable focus distance. */
            CHECK(os_inspect_set(&s, FLD_FOCUS, 1.0 + 0.5 * (double)i));
            CHECK(os_history_record(&h, &before, &s));
        }
        CHECK(h.nundo == OS_HISTORY_DEPTH);

        /* Walk all the way back. Entry i holds the state BEFORE edit i, so
         * entry i is worth 1.0 + 0.5(i-1) -- one edit behind its own index.
         * The kept entries are the last DEPTH of them, i = EXTRA upward, so
         * the oldest state still reachable is 1.0 + 0.5(EXTRA-1) and NOT
         * 1.0 + 0.5*EXTRA, which is the neighbouring off-by-one and the
         * reason this is spelled out rather than asserted from memory. */
        int n = 0;
        while (os_history_undo(&h, &s)) ++n;
        CHECK(n == OS_HISTORY_DEPTH);
        CHECK_NEAR(s.focus_m, 1.0 + 0.5 * (double)(EXTRA - 1), 1e-12);
        NOTE("%d steps kept of %d taken; oldest reachable focus %.2f m",
             n, OS_HISTORY_DEPTH + EXTRA, (double)s.focus_m);
        os_history_free(&h);
    }

    SECTION("history: the viewer's mark loop coalesces a drag into one step");
    {
        /* main.c does not call record at edit sites. It keeps a baseline and
         * closes it off once a frame, and only when no gesture is in flight.
         * That policy is what makes a drag ONE step instead of one per
         * mouse-motion event, and it is reproduced here in miniature -- the
         * module cannot check it, and the window cannot be clicked by a test.
         *
         * The two ways it goes wrong are opposite and both fatal to the
         * feature: mark during a gesture and undoing a drag takes fifty
         * presses; never re-baseline after a step and undo ping-pongs between
         * two states for ever. Both are asserted below. */
        OsHistory h;
        CHECK(os_history_init(&h));
        OsSettings s, base;
        os_settings_default(&s);
        base = s;

        #define MARK()      do { os_history_record(&h, &base, &s); base = s; } while (0)
        /* A frame in which a gesture is still held: nothing is closed off. */
        #define HELD_FRAME() do { } while (0)

        /* --- a drag: many changes, one gesture, one step --- */
        s.sel_obj = os_scenedesc_next_object(&s.scene, 0);
        CHECK(s.sel_obj >= 0);
        MARK();                       /* selecting is not itself an edit */
        int before_steps = h.nundo;
        for (int i = 0; i < 50; ++i) {
            /* One mouse-motion event's worth: the subject slides away from
             * the camera a centimetre at a time. */
            /* Offset by one so the very first motion is a real change: the
             * rail's near target already sits at exactly -1.0 m, and
             * os_inspect_set reports false for a value that did not move. */
            CHECK(os_inspect_set(&s, FLD_O_Z, -1.0 - 0.01 * (double)(i + 1)));
            HELD_FRAME();
        }
        CHECK(h.nundo == before_steps);          /* nothing yet: still held */
        MARK();                                   /* mouse up */
        CHECK(h.nundo == before_steps + 1);       /* exactly one step */

        /* --- idle frames record nothing --- */
        for (int i = 0; i < 20; ++i) MARK();
        CHECK(h.nundo == before_steps + 1);

        /* --- undo, the way main.c does it: mark, step, re-baseline --- */
        OsSettings mid = s;
        MARK();
        CHECK(os_history_undo(&h, &s));
        base = s;                                 /* THE re-baseline */
        CHECK(!same_doc(&s, &mid));

        /* The next frames must record nothing. Without the re-baseline the
         * loop would see base != s, push the undone state straight back on,
         * and undo would toggle for ever between two states. */
        int after_undo = h.nundo;
        for (int i = 0; i < 20; ++i) MARK();
        CHECK(h.nundo == after_undo);
        CHECK(os_history_can_redo(&h));            /* and redo survives */

        CHECK(os_history_redo(&h, &s));
        base = s;
        for (int i = 0; i < 20; ++i) MARK();
        CHECK(same_doc(&s, &mid));
        CHECK(!os_history_can_redo(&h));
        #undef MARK
        #undef HELD_FRAME
        os_history_free(&h);
    }

    SECTION("history: a failed allocation degrades rather than crashes");
    {
        /* os_history_init reports failure and leaves the struct safe to call.
         * Undo missing is better than a window that will not open. */
        OsHistory h;
        memset(&h, 0, sizeof h);      /* as if init had failed */
        OsSettings s;
        os_settings_default(&s);
        OsSettings before = s;
        CHECK(os_inspect_set(&s, FLD_FNO, 4.0));

        CHECK(!os_history_record(&h, &before, &s));
        CHECK(!os_history_undo(&h, &s));
        CHECK(!os_history_redo(&h, &s));
        CHECK(!os_history_can_undo(&h));
        CHECK_NEAR(s.fno, 4.0, 1e-12);    /* the edit itself still stands */
        os_history_free(&h);              /* and freeing nothing is safe */
    }
}
