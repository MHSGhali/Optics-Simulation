/* stage.h — the procedural test scene.
 *
 * THE INVARIANT THIS MODULE OWNS
 *   Every object's distance from the camera is a number this module knows in
 *   closed form and will hand back on request. A depth-of-field or focus claim
 *   is then CHECKABLE against the truth rather than eyeballed.
 *
 *   That is why the scene is generated rather than loaded. A photographed test
 *   chart tells you an image looks sharp; a chart at exactly 2.000 m tells you
 *   whether the lens focused where it said it did.
 *
 * WHY IT STAYS SMALL
 *   The vendored scene traversal is a linear scan over the primitive array,
 *   with BVHs only inside meshes. That is fine at a few hundred primitives and
 *   is what will let moving objects be posed per-ray later without a
 *   motion-aware acceleration structure. Keep the stage under a few hundred
 *   prims, or that trade stops being free.
 */
#ifndef OPTICSIM_STAGE_H
#define OPTICSIM_STAGE_H

#include "opticsim/env.h"
#include "lightsim/scene.h"

/* THE TWO RAILS ARE THE SAME EXPERIMENT, ARRANGED TWO WAYS.
 *
 * Both put five equal targets at 1.0, 1.5, 2.0, 3.0 and 5.0 m and ask which
 * one focusing picks out. They differ only in where the targets sit ACROSS the
 * frame, and that turns out to decide whether the question has a clean answer:
 *
 *   RAIL  a row, spread from one side of the frame to the other. Honest about
 *         what a real lens does off axis, and for that reason a poor control:
 *         the outer targets carry coma and astigmatism the middle ones do not,
 *         so a comparison between them is a comparison of focus AND field at
 *         once. Focused at 5 m, the 5 m target is not the sharpest thing in
 *         frame -- see tests/test_render_focus.c, which pins that.
 *
 *   RING  all five at the SAME angular radius, at five clock positions. Equal
 *         field radius means identical field aberration, which cancels out of
 *         every comparison and leaves defocus as the only difference. This is
 *         the one to use when the question is about focus.
 *
 * Keeping both is the point. The row is what a naive test chart looks like and
 * why it misleads; the ring is the controlled version. Switching between them
 * with one key is the clearest statement this program can make about why field
 * position belongs in the discussion at all.
 */
typedef enum {
    OS_STAGE_DEPTH_RAIL,   /* five targets in a ROW across the frame           */
    OS_STAGE_DEPTH_RING,   /* the same five at one field radius: focus only    */
    OS_STAGE_BOKEH,        /* a field of depth, from 0.55 m to 14 m            */
    OS_STAGE_GRID,         /* a flat dot chart: the one scene that shows       */
                           /* DISTORTION, which moves points instead of        */
                           /* blurring them and so is invisible on anything    */
                           /* round                                            */
    OS_STAGE_COUNT
} OsStageId;

#define OS_STAGE_MAX_MARKERS 32

typedef struct {
    Scene scene;

    /* Light with no geometry, so it lives beside the Scene rather than in it.
     * `on` is false unless the description asked for a dome. */
    OsEnv env;

    /* The ground truth. */
    struct {
        char    label[24];
        int     prim;
        ls_real depth_m;     /* along the camera's view axis */
    } marker[OS_STAGE_MAX_MARKERS];
    int nmarkers;

    /* Where the camera should stand to see it as intended. */
    vec3 cam_eye, cam_target;

    /* Owned allocations, freed by os_stage_free. */
    Prim     *prims;
    Material *mats;
    Light    *lights;
} OsStage;

/* There is no os_stage_build any more. A stage is now a PRESET that seeds an
 * editable description -- see opticsim/scenedesc.h -- and os_scenedesc_build
 * turns that description into this flat, owned Scene. Building straight from a
 * preset id is what used to make the scene unarrangeable: every settings
 * change regenerated it and threw away any edit. */
void        os_stage_free(OsStage *s);
const char *os_stage_name(OsStageId id);

/* Distance of a named marker from the camera, in metres. Negative if unknown. */
ls_real     os_stage_depth(const OsStage *s, const char *label);

#endif /* OPTICSIM_STAGE_H */
