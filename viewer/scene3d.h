/* scene3d.h — the camera and its subjects, seen from outside.
 *
 * WHAT THIS VIEW IS FOR
 *   The lens view shows how the glass bends light; the image view shows what
 *   came out. Neither shows WHERE ANYTHING IS. This one does: the camera, the
 *   cone it sees, the plane it is focused on, the near and far limits of
 *   acceptable sharpness, and the objects, at their true distances.
 *
 *   That is the picture that makes a depth-of-field setting legible. "f/5 at
 *   2 m" is a pair of numbers; a focus plane sitting on the third sphere with
 *   the depth-of-field slab reaching neither of its neighbours is an
 *   explanation.
 *
 * THE INVARIANT THIS MODULE OWNS
 *   Every distance drawn comes from the same source the renderer uses -- the
 *   stage's own marker table for the objects, the lens's own solver for the
 *   focus and depth-of-field planes. Nothing here re-derives a position from a
 *   formula of its own, because a diagram that disagrees with the render is
 *   worse than no diagram.
 *
 * FREE OF SDL
 *   It produces line segments in world space and projects them through its own
 *   orbit camera; draw.c only rasterises the result. So the projection can be
 *   checked headlessly, which matters -- a sign error in a view matrix is
 *   invisible until you already believe the picture.
 */
#ifndef OPTICSIM_VIEWER_SCENE3D_H
#define OPTICSIM_VIEWER_SCENE3D_H

#include "opticsim/lens.h"
#include "opticsim/scenedesc.h"

typedef enum {
    S3_GRID,        /* the ground, and its distance rings   */
    S3_AXIS,        /* the optical axis                     */
    S3_CAMERA,      /* the body and the barrel              */
    S3_FRUSTUM,     /* what the sensor can see              */
    S3_FOCUS,       /* the plane in focus                   */
    S3_DOF,         /* the near and far limits of sharpness */
    S3_OBJECT,      /* a subject, at its true distance      */
    S3_SUBJECT,     /* one inside the depth of field        */
    S3_LIGHT,       /* a lamp                               */
    S3_SKY,         /* the ambient dome, when that is the light */
    S3_SELECTED,    /* whatever the panel is editing        */
    S3_KIND_COUNT
} S3Kind;

typedef struct { vec3 a, b; S3Kind kind; } S3Seg;

/* A label pinned to a world position -- distances, mostly, which is the whole
 * point of the view. */
typedef struct { vec3 at; char text[24]; S3Kind kind; } S3Label;

#define S3_MAX_SEG   4096
#define S3_MAX_LABEL   32

typedef struct {
    S3Seg   seg[S3_MAX_SEG];
    int     nseg;
    S3Label label[S3_MAX_LABEL];
    int     nlabel;

    /* Orbit camera. Spherical about `target`, which is the only sane control
     * for a view whose subject is a line of things going away from you. */
    vec3    target;
    ls_real dist, az, el;

    /* Reported back so the panel can show what the diagram is showing. */
    ls_real near_m, far_m, hyperfocal_m;
} Scene3D;

void s3_init(Scene3D *s);

/* Build the segment list from the ARRANGEMENT, not from the built scene.
 *
 * Drawing what is being edited, rather than what was last rendered, is what
 * lets the diagram respond to a drag before a render has finished -- and it is
 * what gives every drawn thing an id the panel can talk about.
 *
 * `coc_limit_mm` is the blur that still counts as sharp; the depth-of-field
 * slab is drawn where the real lens reaches it. */
void s3_build(Scene3D *s, const OsSceneDesc *d, const OsLens *L,
              ls_real sensor_w_mm, ls_real sensor_h_mm, ls_real coc_limit_mm,
              int sel_obj, int sel_light);

/* ---- picking and dragging ----
 *
 * PICKING IS BY PROJECTED DISTANCE, not by ray intersection. Against a
 * wireframe that is what the eye is doing anyway: a ray test would sail
 * through the middle of a sphere drawn as three rings, and would make a lamp
 * drawn as a small marker almost unclickable. */
#define S3_PICK_PX 16.0

/* Nearest live object or light centre within S3_PICK_PX of the cursor. Sets at
 * most one of *obj / *light and clears the other; both -1 if nothing is near.
 * Lights win ties, because they are drawn smaller. */
void s3_pick(const Scene3D *s, const OsSceneDesc *d, int w, int h,
             ls_real px, ls_real py, int *obj, int *light);

/* Ray from the orbit eye through a pixel -- the exact inverse of s3_project. */
void s3_pick_ray(const Scene3D *s, int w, int h, ls_real px, ls_real py,
                 vec3 *o, vec3 *dir);

/* Where that ray meets the horizontal plane y = plane_y.
 *
 * A fixed plane, deliberately, rather than intersecting the scene under the
 * cursor: that approach has to exclude the dragged object from its own query,
 * or the hit lands on the object's near face, the result is offset along a
 * normal pointing back at the camera, and the object walks into the eye for as
 * long as the button is held. A plane cannot do that. */
bool s3_plane_hit(const Scene3D *s, int w, int h, ls_real px, ls_real py,
                  ls_real plane_y, vec3 *out);

/* Project a world point to pixels inside `w` x `h`. Returns false behind the
 * eye, where no pixel corresponds to it. */
bool s3_project(const Scene3D *s, vec3 p, int w, int h, ls_real *sx, ls_real *sy);

/* Orbit and zoom, clamped so the view cannot be turned inside out. */
void s3_orbit(Scene3D *s, ls_real d_az, ls_real d_el);
void s3_zoom(Scene3D *s, ls_real factor);

#endif /* OPTICSIM_VIEWER_SCENE3D_H */
