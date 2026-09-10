/* camera.h — sensor point plus random numbers, out to a world ray.
 *
 * THE INVARIANT THIS MODULE OWNS
 *   One function turns (pixel, sample index, RNG) into a world-space ray
 *   carrying a wavelength and an importance weight, and it is the ONLY place
 *   millimetres meet metres. Everything upstream of it is lens space, in mm;
 *   everything downstream is scene space, in m.
 *
 *   check-units in the Makefile refuses to let millimetres appear outside the
 *   lens layer, because a radius in mm multiplied by a scene distance in m is a
 *   1000x focus error -- and a 1000x focus error does not crash, it just looks
 *   slightly soft.
 *
 * AND: A VIGNETTED SAMPLE IS STILL A SAMPLE
 *   When a ray is clipped by the glass, os_camera_sample returns false and the
 *   caller must still COUNT it, contributing zero. Retrying until a ray gets
 *   through would renormalise the vignetting away -- the corners would come out
 *   exactly as bright as the centre, which looks entirely plausible and is the
 *   opposite of what a real lens does.
 *
 * THE WEIGHT, DERIVED
 *   Irradiance at a film point from the rear element is
 *
 *       E = INT L cos(theta_f) cos(theta_r) / d^2  dA
 *
 *   Both planes are perpendicular to the axis, so theta_f = theta_r = theta and
 *   d = Z/cos(theta) with Z the axial gap. The kernel collapses to
 *   cos^4(theta)/Z^2, and sampling A uniformly over a box of area A_box gives
 *
 *       weight = A_box cos^4(theta) / Z^2 * T_fresnel(lambda) * inv_pdf_lambda
 *
 *   [mm^2 / mm^2] is dimensionless-as-steradians, so weight * L in
 *   W/(m^2 sr nm) yields W/(m^2 nm): spectral irradiance at the film.
 *
 *   The cos^4 falloff is DERIVED here, not applied. Nothing else in this
 *   program may multiply by a vignetting factor -- see lens.h.
 */
#ifndef OPTICSIM_CAMERA_H
#define OPTICSIM_CAMERA_H

#include "opticsim/lens.h"
#include "opticsim/pupil.h"
#include "opticsim/spectral.h"
#include "lightsim/geom.h"
#include "lightsim/rng.h"

typedef struct {
    /* Pose. The eye sits at the lens's FRONT VERTEX, so racking focus -- which
     * moves the film, not the glass -- leaves the camera's position in the
     * world exactly where it was. */
    vec3    eye, fwd, right, up;

    ls_real sensor_w_mm, sensor_h_mm;
    int     width, height;          /* render grid */

    OsLens        lens;
    OsPupilCache  pupil;
} OsCamera;

typedef struct {
    Ray     ray;          /* WORLD space, metres */
    ls_real lambda_nm;
    int     bin;
    ls_real weight;       /* dimensionless; includes 1/pdf(lambda) */
} OsCameraSample;

/* Build a camera: mount the lens, size the sensor, and cache the pupil.
 * `sensor_w_mm` fixes the format; the height follows from the render aspect. */
bool os_camera_build(OsCamera *c, OsPrescriptionId lens, ls_real efl_mm,
                     ls_real fno, ls_real sensor_w_mm, int w, int h,
                     char *why, size_t nwhy);

void os_camera_look_at(OsCamera *c, vec3 eye, vec3 target, vec3 up_hint);

/* Re-cache the pupil. Needed after any change to aperture, focus or focal
 * length; cheap enough to call unconditionally when something changed. */
void os_camera_refresh(OsCamera *c);

void os_camera_free(OsCamera *c);

/* THE function. Returns false when the ray was vignetted -- the caller counts
 * the sample anyway, at zero. */
bool os_camera_sample(const OsCamera *c, int x, int y, uint64_t sample_index,
                      Rng *rng, OsCameraSample *out);

/* Horizontal field of view, degrees, for reporting. */
ls_real os_camera_hfov_deg(const OsCamera *c);

/* World point to a pixel in the rendered image -- the paraxial inverse of the
 * film mapping in os_camera_sample, including its two y flips and its x flip.
 *
 * Paraxial, and only used to place overlays: a real trace would land a hair
 * away because the lens distorts, and an annotation being a pixel off matters
 * far less than it agreeing with where os_camera_sample would have put it.
 *
 * Returns false for a point at or behind the front vertex, where no pixel
 * corresponds to it. */
bool os_camera_project(const OsCamera *c, vec3 world, ls_real *px, ls_real *py);

#endif /* OPTICSIM_CAMERA_H */
