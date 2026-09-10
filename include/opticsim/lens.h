/* lens.h — a mounted lens: scaled, stopped, focused, and traceable.
 *
 * THE INVARIANT THIS MODULE OWNS
 *   Every ray that reaches the world passed through the clear aperture of
 *   every surface AND through the iris, exactly once each. Nothing is faked,
 *   nothing is clipped twice, and the ONLY vignetting in this program is
 *   geometric -- it emerges from those clips and from nowhere else.
 *
 *   The failure mode that buys is subtle and extremely common: adding a cos^4
 *   falloff term "because lenses darken at the edges". The cos^4 law is
 *   already produced by the pupil-sampling Jacobian in camera.h, so an added
 *   term double-counts it, darkens the corners by roughly a factor of two, and
 *   looks like a tasteful vignette rather than a bug. If a corner is dark
 *   here, it is because rays aimed at it hit the edge of a real element.
 *
 * COORDINATES
 *   Lens space is MILLIMETRES, z along the axis, +z from the object toward the
 *   sensor, origin at the FRONT VERTEX. The scene is metres. Those two units
 *   meet in exactly one function, os_camera_sample(), and check-units in the
 *   Makefile refuses to let millimetres appear outside the lens layer -- a
 *   radius in mm multiplied by a scene distance in metres is a 1000x focus
 *   error, and a 1000x focus error does not look like a crash, it looks
 *   slightly soft.
 *
 * PARAXIAL ANALYSIS
 *   The y-nu trace below is the classical first-order method, and it is worth
 *   having even though a real trace is also implemented, for three reasons:
 *   it defines the focal length and the pupils (which are first-order
 *   properties, not measurements); it runs at any wavelength, which is how
 *   chromatic aberration becomes a NUMBER rather than a coloured fringe you
 *   squint at; and it is the reference the real trace must converge to as the
 *   ray height goes to zero, which is the test that says spherical aberration
 *   is being computed rather than invented.
 *
 *       per surface:  u' = (n u - y (n' - n)/R) / n'      then  y += u' t
 *       EFL = -y_first / u'_last      BFD = -y_last / u'_last
 */
#ifndef OPTICSIM_LENS_H
#define OPTICSIM_LENS_H

#include "opticsim/prescription.h"
#include "lightsim/vec.h"

typedef struct {
    char       name[32];
    OsSurface  surf[OS_MAX_SURF];
    OsGlass    glass[OS_MAX_SURF];   /* resolved once; index AFTER surface i */
    int        nsurf;
    int        stop_index;

    ls_real    f_number;             /* as set; defined by the ENTRANCE pupil */
    ls_real    stop_semi_ap_mm;      /* the iris radius that realises it      */
    ls_real    scale;                /* k applied to the base prescription    */

    /* First-order properties at the d line, all in mm. Computed at build. */
    ls_real    efl_mm;
    ls_real    bfd_mm;               /* rear vertex to rear focal point       */
    ls_real    ffd_mm;               /* front focal point to front vertex     */
    ls_real    pp_rear_mm;           /* rear principal plane, from rear vertex */
    ls_real    ep_z_mm;              /* entrance pupil, from FRONT vertex     */
    ls_real    ep_semi_ap_mm;
    ls_real    ep_mag;               /* stop -> entrance pupil magnification  */
    /* The EXIT pupil: the stop as seen from the image side, imaged by whatever
     * glass sits BEHIND it. It is what subtends the blur cone at the film, so
     * it -- not the entrance pupil -- sets the circle of confusion. On a design
     * whose stop is at the front, like the shipped doublet, the rear elements
     * magnify it by about 1.3, and using the entrance pupil instead
     * under-predicts every defocus blur by that factor. */
    ls_real    xp_z_mm;              /* exit pupil, from the REAR vertex      */
    ls_real    xp_semi_ap_mm;
    ls_real    xp_mag;               /* stop -> exit pupil magnification      */
    ls_real    total_track_mm;       /* front vertex to rear vertex           */
    ls_real    image_circle_mm;

    ls_real    film_z_mm;            /* sensor, from the REAR vertex          */
    ls_real    focus_distance_m;

    /* The iris. Blade count and shape change the BOKEH and the diffraction
     * spikes, and must not change the exposure by one photon -- see
     * os_iris_circumradius() for how the area is held constant. */
    int        blades;               /* < 3 means a perfect circle            */
    ls_real    blade_rot_rad;
    ls_real    blade_curvature;      /* 0 straight-edged polygon, 1 circle    */
} OsLens;

/* Build a lens: resolve glasses, scale to `efl_mm`, set the iris for `fno`,
 * run the first-order analysis, and focus at infinity.
 *
 * Refuses, with a reason in `why`, if the resulting paraxial focal length does
 * not match the design value. Transcription error is the most likely defect in
 * any prescription table, and a lens whose focal length is 12 % off still
 * renders a perfectly convincing image -- of the wrong field of view. Pass
 * efl_mm <= 0 to keep the design's own focal length. */
bool os_lens_build(OsLens *L, OsPrescriptionId id, ls_real efl_mm,
                   ls_real fno, char *why, size_t nwhy);

/* ---- first-order analysis ---- */

/* Full y-nu trace at one wavelength. Any output pointer may be NULL.
 * This is where chromatic aberration is measured rather than merely seen. */
void os_lens_paraxial(const OsLens *L, ls_real lambda_nm,
                      ls_real *efl, ls_real *bfd, ls_real *pp_rear);

/* Effective focal length at a wavelength. os_lens_efl(F) - os_lens_efl(C) over
 * os_lens_efl(d) is the longitudinal chromatic aberration, and for a thin
 * singlet it equals exactly -1/V_d. */
ls_real os_lens_efl_at(const OsLens *L, ls_real lambda_nm);

/* Index of the medium BEFORE surface i (air ahead of surface 0). */
ls_real os_lens_n_before(const OsLens *L, int i, ls_real lambda_nm);
/* Index of the medium AFTER surface i. */
ls_real os_lens_n_after(const OsLens *L, int i, ls_real lambda_nm);

/* Axial z of surface i's vertex, measured from the front vertex. */
ls_real os_lens_vertex_z(const OsLens *L, int i);

/* ---- controls ---- */

/* Set the f-number. The stop radius follows from the ENTRANCE pupil, not from
 * f/(2N) directly: the entrance pupil is the paraxial image of the stop formed
 * by the elements in FRONT of it, and ignoring that magnification is wrong by
 * 10-20 % on a real design -- a quarter of a stop of exposure error that no
 * image would ever reveal. */
bool os_lens_set_fnumber(OsLens *L, ls_real fno);

/* Move the sensor to focus on an object at `distance_m`. HUGE_VAL for
 * infinity. Focusing moves the film, not the glass, so the camera's pose stays
 * anchored at the front vertex and the world does not walk while racking. */
bool os_lens_focus(OsLens *L, ls_real distance_m);

/* Diameter of the defocus blur, in mm on the film, for an object at
 * `object_distance_m` with the lens focused where it currently is.
 *
 * Computed from the EXIT pupil and the actual film position rather than from
 * the textbook thin-lens expression, which silently assumes the two pupils are
 * the same size. Returns 0 at the focused distance. */
ls_real os_lens_coc_mm(const OsLens *L, ls_real object_distance_m);

/* Depth of field: the distances either side of focus at which the defocus blur
 * reaches `coc_limit_mm`.
 *
 * Solved from os_lens_coc_mm() by bisection rather than from the textbook
 * hyperfocal expression, so it inherits whatever the real lens does -- the
 * actual exit pupil, the actual principal planes, the actual film position.
 * For an ideal thin lens the two agree exactly, and the tests assert that.
 *
 * `far_m` comes back as HUGE_VAL once focus reaches the hyperfocal distance,
 * which is the honest answer: everything beyond it is acceptably sharp.
 * Returns false if the lens is not focused on anything finite. */
bool os_lens_dof(const OsLens *L, ls_real coc_limit_mm,
                 ls_real *near_m, ls_real *far_m);

/* The focus distance at which the far limit first reaches infinity. */
ls_real os_lens_hyperfocal_m(const OsLens *L, ls_real coc_limit_mm);

/* The spot an object ACTUALLY makes on the film, RMS diameter in mm, traced
 * through the real glass at its real field position.
 *
 * os_lens_coc_mm above is a paraxial statement and models DEFOCUS ALONE. That
 * is the whole story on axis, and badly incomplete off it: a simple doublet has
 * no correction for coma, astigmatism or field curvature, and by ten or so
 * millimetres of field they dominate. On this achromat at f/5 focused at
 * 4.57 m, a subject at 5 m -- dead centre of the paraxial depth of field --
 * blurs to 0.47 mm at 13.7 mm off axis, while one at 3 m, supposedly well
 * outside it, comes to 0.09 mm at 6.9 mm off axis. The depth-of-field figure
 * says the opposite of what the camera records, and neither is wrong: they are
 * answers to different questions.
 *
 * So anything claiming a subject is sharp asks THIS, not the paraxial one.
 *
 * `object_height_m` is the subject's distance from the optical axis. Returns
 * HUGE_VAL if no ray gets through -- the subject is outside what the lens
 * covers, which is its own kind of "not sharp". */
ls_real os_lens_spot_mm(const OsLens *L, ls_real object_distance_m,
                        ls_real object_height_m, int nrays);

/* Transmittance at one wavelength on axis: the product of (1 - R_fresnel) over
 * every glass-air interface. This is the f-stop / T-stop gap, and it is real:
 * an uncoated double Gauss transmits about 60 %. */
ls_real os_lens_transmittance(const OsLens *L, ls_real lambda_nm);

/* Half the diagonal field angle the design covers, in degrees, given a sensor
 * diagonal. Reports coverage, and warns when the image circle falls short. */
ls_real os_lens_half_fov_deg(const OsLens *L, ls_real sensor_diagonal_mm);

/* ---- real sequential ray tracing ----------------------------------------
 *
 * Lens-local coordinates, MILLIMETRES, +z toward the sensor, origin at the
 * front vertex. `d` is unit length. */
typedef struct { vec3 o, d; } OsLensRay;

/* Intersect a spherical (or plano, radius 0) surface whose vertex sits at
 * z_vertex on the axis. Returns the outward normal UNFLIPPED -- the caller
 * decides orientation, exactly as the vendored geom.h refuses to flip Hit.ng,
 * because silently flipping normals is a whole family of energy-leak bugs. */
bool os_surface_hit(ls_real radius_mm, ls_real z_vertex,
                    const OsLensRay *r, ls_real *t_out, vec3 *n_out);

/* Snell. `eta` is n_in/n_out. Returns false on total internal reflection,
 * which KILLS the ray rather than reflecting it: that light does not reach the
 * film along the intended path, and the reflected branch is the flare pass's
 * business, not the primary trace's. */
bool os_refract(vec3 d, vec3 n, ls_real eta, vec3 *out);

/* Is (x, y) inside the iris? Circular when blades < 3, otherwise the blade
 * polygon, blended toward a circle by blade_curvature. */
bool os_lens_aperture_contains(const OsLens *L, ls_real x_mm, ls_real y_mm);

/* The circumradius an N-blade iris needs in order to enclose the SAME AREA as
 * a circle of radius `a`.
 *
 * The f-number is a statement about how much light gets through, so the iris
 * AREA is what must equal pi a^2. A regular N-gon of circumradius rho has area
 * (N/2) rho^2 sin(2pi/N), so rho = a sqrt(2pi / (N sin(2pi/N))). Without this,
 * switching from a circular iris to seven blades would darken the image by
 * 13 % -- an exposure change with no cause, produced by a control that is
 * supposed to affect only the SHAPE of the blur. */
ls_real os_iris_circumradius(ls_real a, int blades, ls_real curvature);

/* Trace one ray from object space through to the film plane.
 *
 * `r` is updated in place to the ray leaving the rear surface, and
 * `transmittance` is multiplied by (1 - R_fresnel) at each interface. Returns
 * false the moment the ray is clipped by any clear aperture, by the iris, or
 * by total internal reflection -- and a clipped ray is what mechanical
 * vignetting IS. */
bool os_lens_trace(const OsLens *L, ls_real lambda_nm, OsLensRay *r,
                   ls_real *transmittance);

/* Trace from the FILM outward into the world -- the direction a camera ray
 * actually travels. `r` starts on the sensor with a direction of negative z and
 * is updated in place to the ray leaving the front surface.
 *
 * A separate function rather than a flag on os_lens_trace: the surfaces are
 * visited in the opposite order and the indices swap at every interface, so a
 * shared body would be a chain of conditionals through the one loop that has to
 * stay obviously correct. */
bool os_lens_trace_reverse(const OsLens *L, ls_real lambda_nm, OsLensRay *r,
                           ls_real *transmittance);

/* Axial z of the sensor plane, measured from the FRONT vertex like everything
 * else drawn or traced. os_lens.film_z_mm is measured from the REAR vertex,
 * and mixing the two is a focus error the size of the lens. */
ls_real os_lens_film_z(const OsLens *L);

/* The same trace, recording every vertex, for drawing. `blocked_at` is the
 * surface index that stopped the ray, or -1 if it got through. */
#define OS_PATH_MAX (OS_MAX_SURF + 2)
typedef struct {
    vec3 p[OS_PATH_MAX];
    int  n;
    int  blocked_at;
} OsRayPath;

bool os_lens_trace_path(const OsLens *L, ls_real lambda_nm, OsLensRay start,
                        ls_real to_z_mm, OsRayPath *path);

/* Where the marginal and chief rays from an axial object at `distance_m`
 * actually cross the axis behind the lens -- the REAL focus, as opposed to the
 * paraxial one. Their difference is longitudinal spherical aberration, and it
 * is the number that makes focus shift on stopping down visible. */
ls_real os_lens_real_focus_z(const OsLens *L, ls_real lambda_nm,
                             ls_real distance_m, ls_real pupil_fraction);

#endif /* OPTICSIM_LENS_H */
