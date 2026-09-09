/* os_stage.c — the built-in scenes, as descriptions to start from.
 *
 * These used to build a flat Scene directly, which is why nothing could be
 * moved: the scene was regenerated from the preset id on every settings
 * change, so an edit lasted until the next time the aperture moved. They now
 * seed an OsSceneDesc, and os_scenedesc_build turns that into the Scene the
 * tracer reads. A preset is a starting point, not the scene.
 */
#include "opticsim/scenedesc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *os_stage_name(OsStageId id) {
    switch (id) {
        case OS_STAGE_DEPTH_RAIL: return "RAIL";
        case OS_STAGE_BOKEH:      return "BOKEH";
        default:                  return "?";
    }
}

void os_stage_free(OsStage *s) {
    free(s->prims);  s->prims  = NULL;
    free(s->mats);   s->mats   = NULL;
    free(s->lights); s->lights = NULL;
    memset(&s->scene, 0, sizeof s->scene);
    s->nmarkers = 0;
}

ls_real os_stage_depth(const OsStage *s, const char *label) {
    for (int i = 0; i < s->nmarkers; ++i)
        if (strcmp(s->marker[i].label, label) == 0) return s->marker[i].depth_m;
    return -1.0;
}

/* Targets on a rail at exactly known distances, staggered across the frame so
 * they do not occlude one another. This is the depth-of-field article: focus
 * at 2.0 m and the 2.0 m target must be the sharp one. */
static void preset_rail(OsSceneDesc *d) {
    static const ls_real DEPTHS[] = { 1.0, 1.5, 2.0, 3.0, 5.0 };
    static const char *NAMES[]    = { "1M", "1.5M", "2M", "3M", "5M" };
    /* Each target's offset and radius scale WITH its distance, so every one
     * subtends the same angle and lands the same size on the sensor. That is
     * the point of the rail: the only difference between them in the image is
     * how far out of focus they are.
     *
     * Their angular POSITIONS have to differ, though, and that is a separate
     * thing -- at a fixed offset they stack in depth and only the nearest is
     * visible. The fractions stay inside 0.18, the tangent of the half-angle a
     * 100 mm lens covers on full frame. */
    static const ls_real FRAC[] = { -0.140, -0.070, 0.0, 0.070, 0.140 };

    for (int i = 0; i < 5; ++i) {
        int id = os_scenedesc_add_object(d, OS_OBJ_SPHERE);
        if (id < 0) break;
        OsObject *o = &d->obj[id];
        ls_real dist = DEPTHS[i];
        o->centre = v3(FRAC[i] * dist, 0.0, -dist);
        o->radius = 0.030 * dist;
        if (i == 2) {                     /* the focus target is the warm one */
            o->rgb[0] = 0.55; o->rgb[1] = 0.20; o->rgb[2] = 0.18;
        } else {
            o->rgb[0] = 0.75; o->rgb[1] = 0.75; o->rgb[2] = 0.78;
        }
        snprintf(o->name, sizeof o->name, "%s", NAMES[i]);
    }

    /* NO BACKDROP. The targets stand in empty space, and what is behind them
     * is whatever the lighting says is behind them: nothing under LAMPS, the
     * sky itself under AMBIENT.
     *
     * There used to be a plane at -12 m here, on the argument that
     * out-of-focus background is worth seeing. It is, but it is a WALL, and a
     * wall bounces light back onto the subjects, occludes the dome behind
     * them, and gives every silhouette a second edge to be confused with the
     * first. A backdrop is a scene element like any other -- add an object and
     * set its SHAPE to PLANE when a scene wants one.
     */

    /* One big soft key light, off to the side and above, so the spheres are
     * shaded rather than flat and the terminator is visible.
     *
     * 20800 lm at 5500 K is the same lamp the hard-coded stage had, restated
     * in the unit it is now authored in: that scene used 120 W radiant, and a
     * 5500 K blackbody is worth about 173 lm/W inside the simulated band. */
    int li = os_scenedesc_add_light(d, OS_LIGHT_RECT);
    if (li >= 0) {
        OsLight *l = &d->lit[li];
        l->centre  = v3(1.6, 1.8, -1.4);
        l->size_u  = 1.0;
        l->size_v  = 1.0;
        l->flux_lm = 20800.0;
        l->cct_k   = 5500.0;
        snprintf(l->name, sizeof l->name, "KEY");
    }
}

/* Small, very bright sources against nothing at all: defocus these and the
 * blur disc takes the shape of the iris, which is what bokeh IS. The dark
 * ground it used to stand against was a plane at -14 m, and empty space is
 * both blacker and cheaper. */
static void preset_bokeh(OsSceneDesc *d) {
    /* 30 mm lamps at 6 m, in a grid well behind a 2 m focus.
     *
     * The size is a SAMPLING decision. A camera ray reaches an emitter only by
     * landing on it, and for a defocused source the fraction of pupil samples
     * that do is about (source image / blur disc)^2 -- at 3 mm that is 0.001,
     * seven samples in six thousand, which comes out as colour confetti rather
     * than as bokeh. At 30 mm it is 0.12 and the same render converges. */
    for (int gy = -1; gy <= 1; ++gy) {
        for (int gx = -2; gx <= 1; ++gx) {
            int li = os_scenedesc_add_light(d, OS_LIGHT_SPHERE);
            if (li < 0) return;
            OsLight *l = &d->lit[li];
            l->centre  = v3(0.62 * ((ls_real)gx + 0.5), 0.62 * (ls_real)gy, -6.0);
            l->radius  = 0.015;
            /* 5800 lm at 3000 K restates the 45 W the hard-coded stage used;
             * a 3000 K blackbody is worth about 129 lm/W in band. */
            l->flux_lm = 5800.0;
            l->cct_k   = 3000.0;
            snprintf(l->name, sizeof l->name, "LAMP %d", li + 1);
        }
    }
}

void os_scenedesc_preset(OsSceneDesc *d, OsStageId id) {
    memset(d, 0, sizeof *d);
    /* The camera looks down -z from the origin. Every depth in this program is
     * measured from here, which is what makes -centre.z a distance. */
    d->cam_eye    = v3(0.0, 0.0, 0.0);
    d->cam_target = v3(0.0, 0.0, -1.0);

    /* The dome is authored even though the preset starts on lamps, so
     * switching to it lands on a usable scene rather than on black. 2000 lx at
     * 6500 K is a bright overcast day -- the lighting a lightbox or a softbox
     * tent is trying to imitate, and roughly what the rail's key lamp puts on
     * the near targets, so the two modes are comparable at one exposure. */
    d->light_mode    = OS_LIGHT_LAMPS;
    d->ambient_lux   = 2000.0;
    d->ambient_cct_k = 6500.0;

    switch (id) {
        case OS_STAGE_BOKEH:      preset_bokeh(d); break;
        case OS_STAGE_DEPTH_RAIL:
        default:                  preset_rail(d);  break;
    }
}
