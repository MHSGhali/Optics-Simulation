/* scenedesc.h — the scene as something you can arrange.
 *
 * THE INVARIANT THIS MODULE OWNS
 *   An id, once handed out, never means anything else. Deleting leaves a
 *   TOMBSTONE (`alive = false`) rather than compacting the array, and `nobj` is
 *   a high-water mark rather than a population count.
 *
 *   Compacting would renumber every object after the deleted one, and the
 *   selection index -- and any undo snapshot, when one exists -- holds ids. So
 *   a delete would silently repoint the selection at its neighbour: you delete
 *   one sphere and the panel is now editing a different one, with nothing on
 *   screen to say so.
 *
 * WHY THIS EXISTS AT ALL
 *   The stage used to be generated procedurally from an enum on every settings
 *   change, so any edit was wiped the next time the aperture moved. This is the
 *   persistent thing the flat Scene is built FROM. The presets in os_stage.c
 *   now seed one of these rather than building a Scene directly.
 *
 * WHY TOMBSTONES HERE AND COMPACTION IN THE SIBLING REPO
 *   Light-Simulation's sceneedit.c compacts its light array on delete and then
 *   performs TWO separate renumberings -- Light.index, and every
 *   Prim.light_id above the hole -- because there the light array IS the live
 *   scene. Here it is not: os_scenedesc_build regenerates the flat Scene
 *   wholesale, so light indices are assigned fresh on every build and nothing
 *   outside a single build ever holds one. That is the structural advantage of
 *   rebuilding rather than mutating in place, and it is what buys the simpler
 *   rule.
 *
 * A PLAIN POD, DELIBERATELY
 *   Fixed arrays, no pointers. So it can live inside OsSettings, be compared
 *   with memcmp to decide whether the render restarts, be copied to the render
 *   thread by assignment, and -- when undo arrives -- be snapshotted by struct
 *   copy rather than by a refcounted deep clone.
 */
#ifndef OPTICSIM_SCENEDESC_H
#define OPTICSIM_SCENEDESC_H

#include "opticsim/stage.h"

#define OS_MAX_OBJECTS 32
#define OS_MAX_LIGHTS  16

/* ---- limits ----
 *
 * These are not taste. Each one guards a way the vendored light code can be
 * made to abort: ls_light_finalize derives radiance from area and then ASSERTS
 * that the flux implied by the geometry matches the flux it was given, and
 * that the spectral shape integrates to one. A zero-area light divides by
 * zero; a colour temperature below about 1200 K underflows every visible bin
 * so the shape cannot be normalised. Both fire the assert inside a build,
 * nowhere near the control that was dragged. */
#define OS_POS_LIMIT_M    50.0
#define OS_SIZE_MIN_M     1.0e-4
#define OS_SIZE_MAX_M     20.0
#define OS_FLUX_MAX_LM    1.0e7
#define OS_CCT_MIN_K      1200.0
#define OS_CCT_MAX_K      20000.0
#define OS_AMBIENT_MAX_LX 1.0e6

typedef enum { OS_OBJ_SPHERE, OS_OBJ_PLANE, OS_OBJ_KIND_COUNT } OsObjKind;

typedef struct {
    bool      alive;             /* false = tombstone; the id stays reserved  */
    OsObjKind kind;
    vec3      centre;
    ls_real   radius;            /* SPHERE                                    */
    vec3      normal;            /* PLANE                                     */
    ls_real   rgb[3];            /* Lambert reflectance, authored as a colour */
    char      name[24];
} OsObject;

typedef enum { OS_LIGHT_SPHERE, OS_LIGHT_RECT, OS_LIGHT_KIND_COUNT } OsLightKind;

typedef struct {
    bool        alive;
    OsLightKind kind;
    vec3        centre;
    ls_real     radius;          /* SPHERE */
    ls_real     size_u, size_v;  /* RECT   */

    /* AUTHORED IN LUMENS, and that is the number held fixed.
     *
     * Watts are recomputed from the lumens and the colour temperature on every
     * build, so changing the CCT of an 800 lm lamp leaves it an 800 lm lamp --
     * which is what anyone adjusting a colour temperature means. Storing watts
     * instead would silently change the brightness every time the colour
     * moved. ls_watts_from_lumens is the one place the conversion happens. */
    ls_real     flux_lm;
    ls_real     cct_k;
    char        name[24];
} OsLight;

/* WHERE THE LIGHT COMES FROM -- a choice, not a blend.
 *
 * LAMPS is placed light: sources with a position, so things have a lit side, a
 * shadow side and a terminator between them. That is what makes a scene look
 * three-dimensional, and it is what you want when the lighting is the subject.
 *
 * AMBIENT is a uniform dome of the same radiance in every direction. Nothing
 * casts a shadow, nothing has a terminator, and every surface is lit by
 * exactly as much sky as it can see. Flat, and deliberately so: shadow
 * contrast reads as sharpness to the eye, so a scene with no shadows at all is
 * the honest place to judge FOCUS. This program has already had one focus
 * question confused by a terminator.
 *
 * A choice rather than two independent switches because they are answers to
 * the same question, and because the interesting comparison is A against B --
 * a scene half-lit by each is a third thing that answers neither. */
typedef enum {
    OS_LIGHT_LAMPS,      /* the placed sources, as authored */
    OS_LIGHT_AMBIENT,    /* a uniform dome; the lamps are off */
    OS_LIGHT_MODE_COUNT
} OsLightMode;

typedef struct {
    OsObject obj[OS_MAX_OBJECTS];  int nobj;   /* high-water mark, NOT a count */
    OsLight  lit[OS_MAX_LIGHTS];   int nlit;
    vec3     cam_eye, cam_target;

    /* ---- the dome ----
     *
     * Authored as the ILLUMINANCE a surface facing it receives, which is the
     * number a light meter reads and the number an exposure is chosen from.
     * The radiance the tracer wants is that divided by pi, and os_scenedesc_
     * build does the division in the one place lights are authored.
     *
     * Kept even while the mode is LAMPS, so switching back and forth does not
     * lose the setting -- the same reason a lamp's flux survives being
     * deselected. */
    OsLightMode light_mode;
    ls_real     ambient_lux;
    ls_real     ambient_cct_k;
} OsSceneDesc;

/* ---- authoring ---- */

/* Seed from one of the built-in stages. Discards whatever was there. */
void os_scenedesc_preset(OsSceneDesc *d, OsStageId id);

/* Returns the new id, or -1 when full. Never wraps, never reuses a tombstone
 * within a session: an id that named a deleted object must not come back
 * meaning something else. */
int  os_scenedesc_add_object(OsSceneDesc *d, OsObjKind k);
int  os_scenedesc_add_light (OsSceneDesc *d, OsLightKind k);

/* False for an out-of-range or already-dead id, so a double delete is a no-op
 * rather than a corruption. */
bool os_scenedesc_delete_object(OsSceneDesc *d, int id);
bool os_scenedesc_delete_light (OsSceneDesc *d, int id);

int  os_scenedesc_count_objects(const OsSceneDesc *d);
int  os_scenedesc_count_lights (const OsSceneDesc *d);

/* The next live id at or after `from`, or -1. Used to move the selection past
 * the tombstones without the caller knowing they exist. */
int  os_scenedesc_next_object(const OsSceneDesc *d, int from);
int  os_scenedesc_next_light (const OsSceneDesc *d, int from);

/* Clamp every field into its legal range, in place. Called by the build and by
 * the inspector, so a value can never reach the vendored light code out of
 * range whichever path set it. */
void os_scenedesc_clamp_object(OsObject *o);
void os_scenedesc_clamp_light(OsLight *l);
void os_scenedesc_clamp_ambient(OsSceneDesc *d);

/* Re-home the fields a kind actually uses.
 *
 * Called unconditionally on a kind change, never "only if unset" -- that
 * qualifier is a real bug in the sibling repo's history: it re-homed the value
 * the FIRST time only, so cycling a kind twice left a number from the wrong
 * quantity sitting in the field. */
void os_scenedesc_rehome_object(OsObject *o);
void os_scenedesc_rehome_light(OsLight *l);

/* ---- building ---- */

/* Assemble the flat Scene the tracer reads. THE only place an emissive prim is
 * paired with its Light, and the only place a lumen becomes a watt.
 *
 * `out` is fully owned by the caller afterwards and freed with os_stage_free,
 * exactly as os_stage_build's result was. */
bool os_scenedesc_build(const OsSceneDesc *d, OsStage *out);

/* Distance of an object from the camera, along the view axis, in metres.
 * Derived rather than recorded, so the ground truth cannot drift from where
 * the object actually is. Negative if the id is not live. */
ls_real os_scenedesc_depth(const OsSceneDesc *d, int id);

#endif /* OPTICSIM_SCENEDESC_H */
