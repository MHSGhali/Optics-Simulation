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
        case OS_STAGE_DEPTH_RING: return "RING";
        case OS_STAGE_BOKEH:      return "BOKEH";
        case OS_STAGE_GRID:       return "GRID";
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

/* ---- what the two depth rails share ----
 *
 * The same five distances and the same five colours, arranged two ways. Only
 * the LAYOUT differs, which is the whole comparison the pair exists to make --
 * see the note on OsStageId in stage.h. */
static const ls_real RAIL_DEPTHS[5] = { 1.0, 1.5, 2.0, 3.0, 5.0 };
static const char *const RAIL_NAMES[5] = { "1M", "1.5M", "2M", "3M", "5M" };

/* One hue each, so a target can be named in a sentence, and so the five stay
 * apart under a flat sky where neutral greys are nearly indistinguishable.
 *
 * EQUAL LUMINANCE, and that is the part that matters. All five reflect 0.48 of
 * what falls on them and differ only in hue. This scene exists to show that the
 * ONE difference between these targets is how far out of focus they are, and
 * the eye reads brightness as sharpness readily enough to confuse that: the
 * previous colouring made the 2 m target -- the one the default focus picks
 * out, the one you are being invited to judge -- 2.8 times darker than its
 * neighbours, which is a second difference sitting exactly where it does the
 * most harm.
 *
 * Moderate rather than saturated, for two reasons: Smits' reconstruction in
 * ls_spectrum_from_rgb_reflectance is least faithful in the saturated corners,
 * and a reflectance near 1.0 in any band is not a paint anybody has.
 *
 * "Equal" holds to 0.1 % as authored -- every one of these is sRGB luminance
 * 0.480 -- and to about 3 % once uplifted to a spectrum and integrated back
 * against the observer, which is the uplift's own fidelity and not something
 * better numbers here could fix. Against the 2.8x it replaces, 3 % is nothing.
 *
 * The cost of the change is real and worth knowing: a saturated reflectance
 * has a far more structured spectrum than a near-neutral grey, so a
 * hero-wavelength renderer needs more samples to converge on this scene than
 * it did on the old one. tests/test_camera.c's exposure ratio had to go from
 * 96 samples a pixel to 384 for that reason. */
static const ls_real RAIL_RGB[5][3] = {
    { 0.995, 0.343, 0.320 },     /* red    */
    { 0.724, 0.449, 0.072 },     /* amber  */
    { 0.169, 0.599, 0.212 },     /* green  */
    { 0.118, 0.565, 0.706 },     /* cyan   */
    { 0.650, 0.377, 0.996 },     /* violet */
};

/* Place one target: at `dist` metres, offset by (fx, fy) RADIANS off the axis.
 *
 * Both the offset and the radius scale with the distance, so every target
 * subtends the same angle and lands the same size on the sensor whatever depth
 * it sits at. That is what makes either rail a controlled experiment: with the
 * apparent size held fixed, the only thing left to differ is the blur. */
static void rail_target(OsSceneDesc *d, int i, ls_real fx, ls_real fy,
                        ls_real ang_radius) {
    int id = os_scenedesc_add_object(d, OS_OBJ_SPHERE);
    if (id < 0) return;
    OsObject *o = &d->obj[id];
    ls_real dist = RAIL_DEPTHS[i];
    o->centre = v3(fx * dist, fy * dist, -dist);
    o->radius = ang_radius * dist;
    o->rgb[0] = RAIL_RGB[i][0];
    o->rgb[1] = RAIL_RGB[i][1];
    o->rgb[2] = RAIL_RGB[i][2];
    snprintf(o->name, sizeof o->name, "%s", RAIL_NAMES[i]);
}

/* The key both rails are lit by. One big soft light, off to the side and above,
 * so the spheres are shaded rather than flat and the terminator is visible.
 *
 * 20800 lm at 5500 K is the same lamp the hard-coded stage had, restated in the
 * unit it is now authored in: that scene used 120 W radiant, and a 5500 K
 * blackbody is worth about 173 lm/W inside the simulated band. */
static void rail_key(OsSceneDesc *d) {
    int li = os_scenedesc_add_light(d, OS_LIGHT_RECT);
    if (li < 0) return;
    OsLight *l = &d->lit[li];
    l->centre  = v3(1.6, 1.8, -1.4);
    l->size_u  = 1.0;
    l->size_v  = 1.0;
    l->flux_lm = 20800.0;
    l->cct_k   = 5500.0;
    snprintf(l->name, sizeof l->name, "KEY");
}

/* NO BACKDROP, on either rail. The targets stand in empty space, and what is
 * behind them is whatever the lighting says is behind them: nothing under
 * LAMPS, the sky itself under AMBIENT.
 *
 * There used to be a plane at -12 m here, on the argument that out-of-focus
 * background is worth seeing. It is, but it is a WALL, and a wall bounces light
 * back onto the subjects, occludes the dome behind them, and gives every
 * silhouette a second edge to be confused with the first. A backdrop is a scene
 * element like any other -- add an object and set its SHAPE to PLANE when a
 * scene wants one. */

/* Targets in a ROW, staggered across the frame so they do not occlude one
 * another. Focus at 2.0 m and the 2.0 m target is the sharp one.
 *
 * AND AT MOST OTHER FOCUS SETTINGS IT IS NOT, which is why the ring below
 * exists. Spreading targets sideways to stop them stacking in depth buys that
 * at the cost of putting them at five DIFFERENT field angles, and every
 * aberration except defocus grows with field: coma roughly with the angle,
 * astigmatism and field curvature with its square. The outer two here sit at
 * 0.140 of their own distance off axis, which is 59 % of a full-frame sensor's
 * half-diagonal, and the shipped achromat is hopeless out there. Focused at
 * 5 m the 5 m target's traced spot is 0.453 mm across while the 3 m target's
 * is 0.106 mm -- both numbers correct, and the focus dial not picking the
 * sharp one. tests/test_render_focus.c pins exactly that case.
 *
 * So this arrangement is kept for what it HONESTLY shows -- that a test chart
 * laid out the obvious way measures focus and field at once -- and RING is the
 * controlled version of the same experiment. The fractions stay inside 0.18,
 * the tangent of the half-angle a 100 mm lens covers on full frame. */
static void preset_rail(OsSceneDesc *d) {
    static const ls_real FRAC[5] = { -0.140, -0.070, 0.0, 0.070, 0.140 };
    for (int i = 0; i < 5; ++i) rail_target(d, i, FRAC[i], 0.0, 0.030);
    rail_key(d);
}

/* The same five targets at the SAME angular radius, at five clock positions
 * 72 degrees apart.
 *
 * Equal field radius means identical field aberration, which cancels out of
 * every comparison between them and leaves defocus as the only difference --
 * which is what a depth-of-field demonstration has always claimed to show and
 * what the row cannot deliver. Move the row's 5 m target from its 0.140 offset
 * onto the axis and its spot goes from 0.453 mm to 0.023 mm, which is what
 * focusing at 5 m ought to give; the ring is that correction applied to all
 * five at once without letting them stack in depth.
 *
 * IT DOES NOT MAKE THEM SHARP, and it is not meant to. At 0.050 rad every ring
 * target carries the same 0.087 mm of field aberration, well over the 0.030 mm
 * the panel calls sharp, so at f/5 nothing here meets that criterion -- what
 * the ring buys is that the error is IDENTICAL for all five and therefore
 * cancels out of every comparison between them. Stop down or shorten the lens
 * and the whole ring sharpens together.
 *
 * FIELD_RAD is a compromise between two bounds. Larger is worse optically.
 * Smaller crowds the targets: neighbours on a regular pentagon are
 * 2*R*sin(36 deg) = 1.176*R apart, and every target has the same angular radius
 * RAD, so 1.176*R must clear 2*RAD with room to spare. 0.050 against 0.024
 * leaves 0.011 rad of sky between them. It also keeps the whole scene in frame
 * out to about 240 mm of focal length, where the row leaves the sensor at 128.
 *
 * Walked clockwise from the upper left, so consecutive depths are neighbours on
 * the ring rather than jumping across it -- the eye can then follow 1 m to 5 m
 * around the circle instead of hunting for them. */
static void preset_ring(OsSceneDesc *d) {
    static const ls_real FIELD_RAD = 0.050;
    static const ls_real RAD       = 0.024;
    static const ls_real CLOCK_DEG[5] = { 162.0, 90.0, 18.0, -54.0, -126.0 };

    for (int i = 0; i < 5; ++i) {
        ls_real a = CLOCK_DEG[i] * LS_PI / 180.0;
        rail_target(d, i, FIELD_RAD * cos(a), FIELD_RAD * sin(a), RAD);
    }
    rail_key(d);
}

/* A DEPTH OF FIELD, rather than a row of them.
 *
 * Bokeh is the character of what a lens does to everything it is NOT focused
 * on, and this scene is that question asked across the whole range at once: a
 * field of objects from half a metre to fourteen, one thin layer of which is
 * sharp. Read it from the middle outward and you can see the blur grow in both
 * directions, take on the aperture's size at each depth, and dim with distance
 * as the inverse square says it must.
 *
 * WHAT USED TO BE HERE, AND WHY IT IS NOT
 *   Ten small very bright lamps at 6 to 11 m, which defocused into discs the
 *   shape of the iris. That is the other half of the subject and it is a real
 *   one -- a blur disc takes the aperture's shape, and a hexagonal iris is
 *   visible only in a HIGHLIGHT, because only a source small and bright enough
 *   to clip has an edge sharp enough to show the polygon.
 *
 *   Those lamps are gone, and with them the six-blade signature: nothing in
 *   this scene is bright enough to blow out, so every blur here is a soft
 *   round falloff rather than a hard-edged disc. What the scene shows instead
 *   is the thing a lamp field could not -- how the blur behaves on ORDINARY
 *   surfaces, with shading and colour and occlusion, which is what a
 *   photograph is actually made of. The iris shape is still checked, on the
 *   pupil itself, in test_lens's aperture sections.
 *
 * FIVE LAYERS
 *   FOREGROUND   0.55-0.85 m   nearer than focus, blurred the other way
 *   SUBJECT      1.10-1.32 m   sharp at the 1.2 m the docs render focuses on
 *   NEAR MID     1.90-3.20 m   softening, and starting to dim
 *   FAR MID      4.50-7.50 m   softer again
 *   DEEP        10.00-14.00 m  where the blur has stopped growing and only the
 *                              light is still falling away
 *
 * The blur stops growing because it asymptotes: an object at infinity images a
 * fixed distance from one at 14 m, so past a certain depth extra distance buys
 * darkness rather than softness. Having objects on both sides of that knee is
 * the only way to see it.
 *
 * STILL NO BACKDROP. Empty space is blacker than any wall and costs nothing to
 * trace. Depth here comes from the objects, from their falloff and from what
 * occludes what -- not from a surface behind them.
 */

/* One row is one sphere. Position and size are authored as ANGLES -- radians
 * off the axis, and an angular radius -- rather than as metres.
 *
 * That is the frame's own coordinate system, and it is the one the choices
 * were actually made in: what matters compositionally is where something lands
 * in the picture and how big it looks, not how many centimetres across it is.
 * Metres follow by multiplying through by the depth, which is done once in the
 * loop below. Authoring in metres instead means every position has the depth
 * multiplied into it by hand, and moving an object in depth then silently
 * moves it across the frame as well.
 *
 * A 100 mm lens on a 36 x 24 mm frame sees +/-0.18 rad across and +/-0.12 up,
 * so those are the frame edges. Rows that exceed them are cropped on purpose:
 * the foreground three are half out of shot the way a real foreground occluder
 * is, and a few of the deep ones grace an edge so the field reads as
 * continuing past the frame rather than as an arrangement that stops at it.
 *
 * Physical sizes end up spanning 0.019 m to 0.360 m and apparent sizes a
 * factor of 5.4, which is the variety the scene is for -- a field of equal
 * discs says nothing about how blur scales. */
typedef struct {
    ls_real depth_m;             /* along the view axis, from the camera      */
    ls_real ax, ay;              /* offset off the axis, RADIANS              */
    ls_real ar;                  /* angular radius, RADIANS                   */
    ls_real r, g, b;             /* Lambert reflectance, authored as a colour */
    const char *name;
} BokehSphere;

static const BokehSphere BOKEH_SPHERES[] = {
    /* ---- foreground: nearer than focus, and LIT, because a dark shape
     * against empty space is not a shape at all. All three sit at a frame edge
     * and blur hard enough to read as a wash of colour rather than as an
     * object, which is exactly what a foreground occluder does. */
    {  0.55, -0.200, -0.130, 0.075,  0.22, 0.42, 0.20, "FG LEAF" },
    {  0.70,  0.210,  0.110, 0.055,  0.52, 0.32, 0.16, "FG TWIG" },
    {  0.85, -0.020,  0.130, 0.022,  0.80, 0.62, 0.60, "FG DUST" },

    /* ---- subject: the sharp layer, and the reference everything else is
     * blurred RELATIVE to. Four sizes and four colours inside 220 mm of depth,
     * far enough apart in the frame that each has its own terminator.
     *
     * THREE, and inside 0.06 rad of the axis, and both of those are the LENS's
     * limits rather than matters of taste.
     *
     * The shipped achromat covers a 20 mm image circle, so 0.10 rad is where
     * its coverage ends -- and it is badly soft well before that. Traced at
     * 1.2 m: 0.11 mm at 0.05 rad off axis, 0.21 mm at 0.063, 0.32 mm at 0.086,
     * 0.47 mm at 0.119. A fourth sphere was drafted into this layer and had to
     * be moved out to 2.0 m, because there is no room for four of these radii
     * inside a disc that small and putting it further out made it three times
     * softer than its neighbours -- a "sharp layer" with a soft member in it
     * is not a layer, it is a contradiction.
     *
     * Everything BEHIND is free to sit wider, because being softened by field
     * on top of defocus is only a problem for the things claiming to be
     * sharp. */
    {  1.20, -0.030, -0.012, 0.038,  0.74, 0.22, 0.16, "HERO RED" },
    {  1.28,  0.040,  0.018, 0.026,  0.16, 0.56, 0.58, "TEAL"     },
    {  1.10,  0.010, -0.056, 0.016,  0.88, 0.83, 0.68, "IVORY"    },

    /* ---- near mid: the first stop out. Still clearly objects, visibly soft,
     * and beginning to lose the key -- illuminance falls as one over r squared
     * and nothing here pretends otherwise. */
    {  2.00, -0.115,  0.062, 0.026,  0.18, 0.30, 0.72, "COBALT" },
    {  1.90, -0.128, -0.058, 0.030,  0.38, 0.43, 0.54, "SLATE"  },
    {  2.20,  0.140,  0.058, 0.020,  0.78, 0.66, 0.42, "SAND"  },
    {  2.60,  0.118, -0.062, 0.034,  0.56, 0.53, 0.20, "OLIVE" },
    {  3.20, -0.025,  0.090, 0.030,  0.46, 0.24, 0.48, "PLUM"  },

    /* ---- far mid: shapes rather than objects now. */
    {  4.50,  0.048,  0.095, 0.024,  0.36, 0.50, 0.26, "MOSS"  },
    {  5.80, -0.150, -0.005, 0.026,  0.66, 0.34, 0.18, "RUST"  },
    {  7.50,  0.030, -0.098, 0.028,  0.72, 0.72, 0.76, "PEARL" },

    /* ---- deep: past the knee, where more distance stops adding blur and only
     * takes light away. Three of them so the effect is a trend and not an
     * anecdote. */
    { 10.00, -0.088, -0.098, 0.026,  0.24, 0.26, 0.58, "INDIGO" },
    { 12.00,  0.155, -0.015, 0.030,  0.80, 0.56, 0.18, "AMBER"  },
    { 14.00, -0.012,  0.038, 0.014,  0.86, 0.88, 0.90, "SNOW"   },
};

/* Every one of these is OUT OF SHOT AT EVERY FOCAL LENGTH, and that is the
 * whole of what separates a light from a subject here -- placement, not kind.
 * Drag one into frame in the viewer and it becomes a glowing ball like any
 * other object.
 *
 * AT EVERY FOCAL LENGTH is the hard part, and the first arrangement failed it.
 * The frame is +/-0.18 rad across at 100 mm but +/-0.75 at 24 mm and +/-1.5 at
 * 12 mm, which is the bottom of the viewer's range -- so lamps tucked just
 * outside a 100 mm frame come sailing INTO shot as soon as anyone winds the
 * lens wide, and a lamp in shot at these fluxes is a blown white ellipse
 * sitting on top of whatever it was lighting. That is what used to happen at
 * 50 mm and below.
 *
 * The fix is to clear the frame VERTICALLY, because the frame is shorter than
 * it is wide: half-height is 0.12 rad at 100 mm, so 1.0 rad at 12 mm, and a
 * lamp more than 1.1 of its own depth above the axis is outside the picture at
 * any focal length this program offers. Three of these hang above the scene
 * like a studio rig; FILL cannot, since its whole job is to come from below,
 * so it clears sideways instead at 1.66 of its depth. The margins put every
 * one of them out of frame down to about 10 mm, past the end of the range.
 *
 * Hanging them that high is what the fluxes pay for -- FAR is ten metres up
 * and 210 000 lm, which is a large fixture but the kind of thing that exists.
 * The alternative was lamps that only behave at one focal length.
 *
 * FOUR of them, because one cannot do it. Illuminance falls as one over r
 * squared, so a key placed to light the subject at 1.2 m has a four-hundredth
 * of that left by 14 m: lit by the key alone this scene is a bright foreground
 * in front of a black hole. Each lamp below owns a stretch of the depth range,
 * and they overlap enough that nothing sits in a gap between them.
 *
 * The levels are aimed at the RAIL's key rather than chosen in the abstract --
 * that stage puts something near 800 lx on its targets and is legible at the
 * default exposure of 100, so these are aimed at the same order and this stage
 * needs no exposure setting of its own.
 *
 *   KEY   high and right, a 0.7 m softbox. ~700 lx on the subject layer, and
 *         the only source soft enough to draw a terminator rather than a line.
 *   FILL  low and opposite, a fifth of the key and much cooler. Without it the
 *         shadow side of every sphere is exactly black and they read as flat
 *         cut-outs instead of as spheres.
 *   MID   behind and high, for 2 to 5 m, where the key has already gone.
 *   FAR   further back and brighter, for everything past 6 m. It still leaves
 *         the deep layer at a quarter to a third of the subject's illuminance,
 *         because that falloff IS the depth cue, and flattening it out would
 *         trade the scene's sense of distance for an even exposure nobody
 *         asked for.
 *
 * Which makes the layers roughly 980, 750, 390 and 200 lx from front to back --
 * the subject aimed at the rail's own level so one exposure serves both stages.
 *
 * Very nearly MONOTONE in depth, which is a side effect of hanging the lamps
 * overhead rather than something aimed at: a lamp high above the axis is about
 * equally far from everything at a given depth, where one off to the side is
 * much closer to the objects on its own side. The earlier side-lit arrangement
 * had the 5.8 m objects coming out darker than the 12 m ones for exactly that
 * reason. Neither is wrong -- what reaches a surface depends on its distance
 * from the LAMP and not from the camera -- but a falloff that tracks depth is
 * the one that reads as depth. */

/* FILL, MID and FAR are SPHERES rather than softboxes for a reason worth
 * knowing: a rect light here is built from ex along +x and ey along +z, so its
 * normal is (0,-1,0) and it always faces DOWN, and rect emission is one-sided.
 * A rect placed below or behind a subject lights nothing at all. A sphere
 * radiates every way and can go anywhere. */
typedef struct {
    OsLightKind kind;
    ls_real x, y, z;
    ls_real size;                /* radius for a sphere, edge for a rect      */
    ls_real flux_lm;
    ls_real cct_k;
    const char *name;
} BokehLight;

static const BokehLight BOKEH_LIGHTS[] = {
    { OS_LIGHT_RECT,    0.95,  1.25, -1.05, 0.70,   5800.0, 5200.0, "KEY"  },
    { OS_LIGHT_SPHERE, -2.40, -0.50, -1.45, 0.10,   7700.0, 7000.0, "FILL" },
    { OS_LIGHT_SPHERE,  1.30,  4.00, -3.60, 0.14,  58000.0, 4000.0, "MID"  },
    { OS_LIGHT_SPHERE,  2.60, 10.00, -9.00, 0.30, 210000.0, 4500.0, "FAR"  },
};

/* ---- and the same field again, further out ----
 *
 * The seventeen above are composed for a 100 mm frame, which sees +/-0.18 rad.
 * Wind the focal length down and that frame opens up fast -- +/-0.36 at 50 mm,
 * +/-0.75 at 24 mm -- and a scene that stops at 0.18 turns into a small
 * huddle of objects in the middle of a lot of black. So there is a second
 * field, from 0.22 rad out to 0.80, which exists to be there when the lens
 * goes wide.
 *
 * GENERATED, where the core is written out, and the split is deliberate. Every
 * position in the table above was chosen against its neighbours -- what
 * occludes what, what sits in whose gap -- because at 100 mm those choices are
 * the picture. None of this is ever in that picture, and at 24 mm nobody is
 * studying the arrangement of the periphery; what matters out here is only
 * that the coverage is even and the objects do not clump. A formula does that
 * better than a person, and in eight lines rather than thirty-two rows.
 *
 * VOGEL'S SPIRAL, which is the arrangement a sunflower head uses. Successive
 * points are one GOLDEN ANGLE apart -- 2.39996 rad, the most irrational
 * fraction of a turn there is -- so no two ever line up however far the
 * sequence runs, and the radius grows as the square root of the index, which
 * is what makes the density per unit AREA constant instead of piling
 * everything into the middle. Even coverage, no clumps, no repeats, and
 * deterministic: the same thirty-two objects every time, which a scene that
 * has to be rendered identically twice requires.
 *
 * Sparser than the core by about seven times, and that is a choice rather
 * than a limit. Matching the core's density out to 0.80 rad would take some
 * three hundred spheres, every one of them scanned by every ray -- and a
 * periphery that thins with distance from the axis reads as a field carrying
 * on past the frame, where a uniform one reads as a wall. */
#define BOKEH_WIDE 32

/* Depths, sizes and colours cycle on lengths that share no factor with each
 * other or with the spiral, so a run of neighbours never repeats a
 * combination. 7, 5 and 11 are all prime and all coprime to 32. */
static const ls_real WIDE_DEPTH[7]  = { 1.6, 2.9, 4.2, 6.5, 9.0, 11.5, 3.6 };
static const ls_real WIDE_ANGRAD[5] = { 0.030, 0.046, 0.022, 0.038, 0.058 };
static const ls_real WIDE_RGB[11][3] = {
    { 0.72, 0.28, 0.22 },  { 0.24, 0.48, 0.66 },  { 0.68, 0.58, 0.24 },
    { 0.34, 0.56, 0.32 },  { 0.56, 0.32, 0.62 },  { 0.78, 0.70, 0.62 },
    { 0.22, 0.52, 0.52 },  { 0.74, 0.46, 0.24 },  { 0.40, 0.40, 0.68 },
    { 0.62, 0.66, 0.32 },  { 0.52, 0.24, 0.34 },
};

static void bokeh_wide_field(OsSceneDesc *d) {
    /* pi * (3 - sqrt 5), the golden angle. */
    const ls_real GOLDEN = 2.399963229728653;
    const ls_real R0 = 0.22, R1 = 0.80;

    for (int i = 0; i < BOKEH_WIDE; ++i) {
        /* Equal AREA per object: interpolate the SQUARE of the radius, or the
         * spiral crowds its own centre and leaves the rim bare. */
        ls_real t   = ((ls_real)i + 0.5) / (ls_real)BOKEH_WIDE;
        ls_real rho = sqrt(R0 * R0 + t * (R1 * R1 - R0 * R0));
        ls_real th  = GOLDEN * (ls_real)i;

        int id = os_scenedesc_add_object(d, OS_OBJ_SPHERE);
        if (id < 0) return;                       /* full: the core comes first */
        OsObject *o = &d->obj[id];

        ls_real depth = WIDE_DEPTH[i % 7];
        o->centre = v3(rho * cos(th) * depth, rho * sin(th) * depth, -depth);
        o->radius = WIDE_ANGRAD[i % 5] * depth;
        o->rgb[0] = WIDE_RGB[i % 11][0];
        o->rgb[1] = WIDE_RGB[i % 11][1];
        o->rgb[2] = WIDE_RGB[i % 11][2];
        snprintf(o->name, sizeof o->name, "WIDE %d", i + 1);
    }
}

static void preset_bokeh(OsSceneDesc *d) {
    for (size_t i = 0; i < sizeof BOKEH_SPHERES / sizeof BOKEH_SPHERES[0]; ++i) {
        const BokehSphere *b = &BOKEH_SPHERES[i];
        int id = os_scenedesc_add_object(d, OS_OBJ_SPHERE);
        if (id < 0) break;
        OsObject *o = &d->obj[id];
        /* Angles to metres, in the one place it happens. */
        o->centre = v3(b->ax * b->depth_m, b->ay * b->depth_m, -b->depth_m);
        o->radius = b->ar * b->depth_m;
        o->rgb[0] = b->r; o->rgb[1] = b->g; o->rgb[2] = b->b;
        snprintf(o->name, sizeof o->name, "%s", b->name);
    }

    /* AFTER the core, so that if the object array ever fills up it is the
     * periphery that is lost rather than the picture. */
    bokeh_wide_field(d);

    for (size_t i = 0; i < sizeof BOKEH_LIGHTS / sizeof BOKEH_LIGHTS[0]; ++i) {
        const BokehLight *b = &BOKEH_LIGHTS[i];
        int li = os_scenedesc_add_light(d, b->kind);
        if (li < 0) break;
        OsLight *l = &d->lit[li];
        l->centre = v3(b->x, b->y, b->z);
        if (b->kind == OS_LIGHT_RECT) { l->size_u = b->size; l->size_v = b->size; }
        else                          { l->radius = b->size; }
        l->flux_lm = b->flux_lm;
        l->cct_k   = b->cct_k;
        snprintf(l->name, sizeof l->name, "%s", b->name);
    }
}

/* A FLAT CHART OF DOTS, which is the only kind of scene that can show
 * distortion at all.
 *
 * Every other aberration in this program spreads a point into a patch, so a
 * sphere is enough to see it: the edge goes soft. Distortion spreads nothing.
 * It takes the image of a point and moves it, whole and sharp, to the wrong
 * radius -- so on a field of round blobs it is invisible, because a blob moved
 * slightly outward is still a blob. What it needs is points that OUGHT to be
 * collinear. A rectilinear lens maps straight lines in the world to straight
 * lines on the film; the amount by which these rows fail to stay straight IS
 * the distortion, and the DISTORTION row in the panel is the same fact as a
 * number.
 *
 * Nine columns by seven rows on one plane at 2 m, evenly spaced in METRES --
 * which is what makes them collinear, and is the whole design of the target.
 * Even spacing in metres on a plane is even spacing in tan(theta), and a
 * distortion-free lens images that to even spacing on the film; anything else
 * is the lens.
 *
 * SIZED FOR 35 MM, which is a compromise the numbers force. The chart's
 * angular extent is fixed once its metres are, and the frame's is not: a
 * 36 x 24 mm sensor sees +/-0.18 rad at 100 mm and +/-0.75 at 24. No chart of
 * sixty-three dots can be dense at one end and full at the other. 35 mm fills
 * exactly, 24 mm shows the whole chart inside a wider frame, and 100 mm shows
 * the middle three by three -- which is honest, because at 100 mm the corner
 * only reaches 0.21 rad and the achromat's distortion there is -0.005 %. There
 * is nothing to see at 100 mm, and the chart correctly shows nothing.
 *
 * Wind it out to 24 mm on the SINGLET and the rows bow visibly: -2.1 %, about
 * ten pixels of sag at the corner of a 720 px frame. On the achromat at the
 * same setting it is -0.45 %, which is the comparison the pair is for. */
static void preset_grid(OsSceneDesc *d) {
    const int COLS = 9, ROWS = 7;
    const ls_real DEPTH = 2.0;
    /* Half-extents that fill a 36 x 24 mm frame at 35 mm. */
    const ls_real HALF_W = DEPTH * 18.0 / 35.0;
    const ls_real HALF_H = DEPTH * 12.0 / 35.0;
    const ls_real RAD    = 0.028;      /* leaves 0.17 m of sky between dots */

    for (int r = 0; r < ROWS; ++r) {
        for (int c = 0; c < COLS; ++c) {
            int id = os_scenedesc_add_object(d, OS_OBJ_SPHERE);
            if (id < 0) return;
            OsObject *o = &d->obj[id];
            ls_real fx = (ls_real)(2 * c - (COLS - 1)) / (ls_real)(COLS - 1);
            ls_real fy = (ls_real)(2 * r - (ROWS - 1)) / (ls_real)(ROWS - 1);
            o->centre = v3(fx * HALF_W, fy * HALF_H, -DEPTH);
            o->radius = RAD;

            /* Uniform, so the eye is judging POSITION and nothing else -- a
             * chart with a colour gradient across it invites you to read the
             * gradient. The centre dot is warm only to mark the axis, which is
             * the one place distortion is zero by definition and therefore the
             * reference the rest are bowing away from. */
            bool centre = (c == COLS / 2) && (r == ROWS / 2);
            o->rgb[0] = centre ? 0.80 : 0.78;
            o->rgb[1] = centre ? 0.28 : 0.78;
            o->rgb[2] = centre ? 0.22 : 0.80;
            snprintf(o->name, sizeof o->name, "%c%d", 'A' + r, c + 1);
        }
    }

    /* ---- lit from BEHIND THE CAMERA ----
     *
     * The cleanest place a lamp can possibly be. Camera rays leave the film
     * and travel toward -z; a source at positive z is behind them and can
     * never be photographed at ANY focal length, which the bokeh scene has to
     * work for by hanging its lamps overhead. It is also the right light for a
     * chart: near-frontal, so every dot shows the same face and none of them
     * has a terminator to be mistaken for a shift in position.
     *
     * Two of them, off to either side, far enough back that the falloff across
     * the chart is about a tenth -- a chart lit brighter on one side reads as a
     * gradient, and this scene is about geometry. */
    static const ls_real LX[2] = { 0.60, -0.60 };
    static const ls_real LY[2] = { 0.45, -0.45 };
    for (int i = 0; i < 2; ++i) {
        int li = os_scenedesc_add_light(d, OS_LIGHT_SPHERE);
        if (li < 0) return;
        OsLight *l = &d->lit[li];
        l->centre  = v3(LX[i], LY[i], 1.30);
        l->radius  = 0.12;
        l->flux_lm = 30000.0;
        l->cct_k   = 5200.0;
        snprintf(l->name, sizeof l->name, "%s", i == 0 ? "KEY" : "FILL");
    }
}

void os_scenedesc_preset(OsSceneDesc *d, OsStageId id) {
    memset(d, 0, sizeof *d);
    /* The camera looks down -z from the origin. Every depth in this program is
     * measured from here, which is what makes -centre.z a distance. */
    d->cam_eye    = v3(0.0, 0.0, 0.0);
    d->cam_target = v3(0.0, 0.0, -1.0);

    /* The dome is authored even though the preset starts on lamps, so
     * switching to it lands on a usable scene rather than on black. 6500 K is
     * daylight; a lightbox or a softbox tent is the thing this imitates.
     *
     * 800 LX, AND THE NUMBER IS MEASURED RATHER THAN CHOSEN. This said 2000
     * for a long time, on the stated grounds that it was "roughly what the
     * rail's key lamp puts on the near targets, so the two modes are
     * comparable at one exposure". The first half was wrong and so the second
     * half was too: the rail's key delivers 782 lx at the 2 m target, and at
     * 2000 the dome put the brightest pixel 2.5x over the lamps -- switching
     * lighting mode blew the subjects out and the claim in this comment was
     * exactly what stopped anyone checking. At 800 the two modes peak at 0.71
     * and 0.70 of white at the same exposure, which is what "comparable" was
     * always supposed to mean. */
    d->light_mode    = OS_LIGHT_LAMPS;
    d->ambient_lux   = 800.0;
    d->ambient_cct_k = 6500.0;

    switch (id) {
        case OS_STAGE_BOKEH:      preset_bokeh(d); break;
        case OS_STAGE_GRID:       preset_grid(d);  break;
        case OS_STAGE_DEPTH_RING: preset_ring(d);  break;
        case OS_STAGE_DEPTH_RAIL:
        default:                  preset_rail(d);  break;
    }
}
