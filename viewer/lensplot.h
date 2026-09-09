/* lensplot.h — the lens cross-section, as geometry.
 *
 * THE INVARIANT THIS MODULE OWNS
 *   Every ray drawn in the diagram came out of os_lens_trace_path(), the same
 *   function the renderer uses. The picture is never a schematic of what the
 *   lens is supposed to do.
 *
 *   That matters more here than anywhere else in the program. A diagram drawn
 *   from the prescription's nominal shape rather than from traced rays would
 *   look completely convincing and would agree with the rendered image only by
 *   coincidence -- and the entire point of having a diagram is to be able to
 *   trust it when the image looks wrong.
 *
 * Free of SDL: this produces polylines in millimetres, and draw.c maps them to
 * pixels. So the surface profiles and the ray fan can be checked headlessly.
 */
#ifndef OPTICSIM_VIEWER_LENSPLOT_H
#define OPTICSIM_VIEWER_LENSPLOT_H

#include "opticsim/lens.h"

#define LP_PROFILE_PTS 129     /* points along one surface's arc; dense
                                * enough that filling an element between
                                * two profiles leaves no gaps */
#define LP_MAX_RAYS     33
#define LP_MAX_ELEMENTS (OS_MAX_SURF)

/* One surface's profile in the meridional (x, z) plane, millimetres, running
 * from -semi_aperture to +semi_aperture. */
typedef struct {
    ls_real x[LP_PROFILE_PTS];
    ls_real z[LP_PROFILE_PTS];
    int     n;
    bool    is_stop;
    bool    solid_after;   /* glass, not air, between this surface and the next:
                            * the two together bound one element's BODY */
} LpProfile;

/* One traced ray, in millimetres, plus how it ended. */
typedef struct {
    ls_real x[OS_PATH_MAX];
    ls_real z[OS_PATH_MAX];
    int     n;
    bool    blocked;          /* clipped by an aperture or lost to TIR */
    ls_real lambda_nm;
    ls_real cross_z;          /* where it crosses the axis, from the rear     */
                              /* vertex; HUGE_VAL if it never does            */
} LpRay;

typedef struct {
    LpProfile profile[LP_MAX_ELEMENTS];
    int       nprofiles;

    LpRay     ray[LP_MAX_RAYS * 3];   /* up to three wavelengths per fan */
    int       nrays;

    /* Extent of everything drawn, in mm, so draw.c can fit it to the panel
     * without knowing any optics. */
    ls_real   z_min, z_max, x_max;

    ls_real   rear_z;                 /* rear vertex, from the front one */
    ls_real   film_z;                 /* from the front vertex */
    ls_real   paraxial_focus_z;       /* from the front vertex */
    ls_real   stop_z, stop_semi_ap;
} LensPlot;

/* Build the whole cross-section.
 *
 * `nrays` fan lines are launched parallel to the axis, evenly spaced across
 * the entrance pupil (and slightly beyond it, so the ones that get clipped are
 * visible as clipped -- vignetting you can see is worth more than vignetting
 * you infer). With `chromatic`, each is traced at F, d and C. */
void lp_build(LensPlot *lp, const OsLens *L, int nrays, bool chromatic,
              ls_real object_distance_m);

#endif /* OPTICSIM_VIEWER_LENSPLOT_H */
