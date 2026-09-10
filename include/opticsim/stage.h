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

typedef enum {
    OS_STAGE_DEPTH_RAIL,   /* identical targets at staged distances: the       */
                           /* depth-of-field article                           */
    OS_STAGE_BOKEH,        /* small bright spheres, to show the iris shape     */
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
