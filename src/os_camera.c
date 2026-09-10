/* os_camera.c — the one place lens millimetres meet scene metres.
 * See camera.h for the derivation of the sample weight. */
#include "opticsim/camera.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define MM_PER_M 1000.0

bool os_camera_build(OsCamera *c, OsPrescriptionId lens, ls_real efl_mm,
                     ls_real fno, ls_real sensor_w_mm, int w, int h,
                     char *why, size_t nwhy) {
    memset(c, 0, sizeof *c);
    if (!os_lens_build(&c->lens, lens, efl_mm, fno, why, nwhy)) return false;

    c->width = w;
    c->height = h;
    c->sensor_w_mm = sensor_w_mm;
    /* The sensor's aspect follows the render grid, so a square render is a
     * square crop of the format rather than a stretched frame. */
    c->sensor_h_mm = sensor_w_mm * (ls_real)h / (ls_real)w;

    os_camera_look_at(c, v3(0, 0, 0), v3(0, 0, 1), v3(0, 1, 0));
    os_camera_refresh(c);
    return true;
}

void os_camera_look_at(OsCamera *c, vec3 eye, vec3 target, vec3 up_hint) {
    c->eye   = eye;
    c->fwd   = v3norm(v3sub(target, eye));
    c->right = v3norm(v3cross(c->fwd, up_hint));
    /* Rebuild up from the orthogonalised right so the basis stays orthonormal
     * even when up_hint is not perpendicular to the view direction -- the same
     * reason the vendored ls_camera_look_at does it. */
    c->up    = v3cross(c->right, c->fwd);
}

void os_camera_refresh(OsCamera *c) {
    os_pupil_free(&c->pupil);
    ls_real diag = 0.5 * sqrt(c->sensor_w_mm * c->sensor_w_mm
                            + c->sensor_h_mm * c->sensor_h_mm);
    os_pupil_build(&c->pupil, &c->lens, diag, 32);
}

void os_camera_free(OsCamera *c) {
    os_pupil_free(&c->pupil);
}

ls_real os_camera_hfov_deg(const OsCamera *c) {
    return 2.0 * atan2(c->sensor_w_mm * 0.5, c->lens.efl_mm) * 180.0 / LS_PI;
}

/* Lens space (mm, +z toward the sensor) to world space (m, +fwd toward the
 * scene). The z axis FLIPS: travelling toward the sensor is travelling away
 * from what the camera is looking at. */
static vec3 lens_dir_to_world(const OsCamera *c, vec3 d) {
    return v3add(v3add(v3scale(c->right, d.x), v3scale(c->up, d.y)),
                 v3scale(c->fwd, -d.z));
}

static vec3 lens_point_to_world(const OsCamera *c, vec3 p) {
    vec3 off = v3add(v3add(v3scale(c->right, p.x), v3scale(c->up, p.y)),
                     v3scale(c->fwd, -p.z));
    return v3add(c->eye, v3scale(off, 1.0 / MM_PER_M));
}

bool os_camera_sample(const OsCamera *c, int x, int y, uint64_t sample_index,
                      Rng *rng, OsCameraSample *out) {
    const OsLens *L = &c->lens;

    uint32_t hash = os_pixel_hash(x, y);
    OsWavelength wl = os_lambda_pick(sample_index, hash);
    out->lambda_nm = wl.lambda_nm;
    out->bin       = wl.bin;
    out->weight    = 0.0;

    /* Pixel to sensor point, in millimetres.
     *
     * TWO sign flips on y, and they are not the same flip. The first is the
     * usual image convention: row 0 is the TOP of the picture, while +y on the
     * sensor points up. The second is physics: a lens forms an INVERTED image,
     * so a point high on the sensor sees the world below the axis. Applying one
     * and not the other gives a vertically mirrored render that looks entirely
     * plausible until something in the scene is not symmetric. Here they cancel
     * on y and leave a single flip on x, which is the lens inversion alone. */
    ls_real px = (ls_real)x + ls_rng_f(rng);
    ls_real py = (ls_real)y + ls_rng_f(rng);
    ls_real fx = -(px / (ls_real)c->width  - 0.5) * c->sensor_w_mm;
    ls_real fy =  (py / (ls_real)c->height - 0.5) * c->sensor_h_mm;

    ls_real film_z = os_lens_film_z(L);
    vec3 film = v3(fx, fy, film_z);

    ls_real rx, ry, area;
    if (!os_pupil_sample(&c->pupil, fx, fy, ls_rng_f(rng), ls_rng_f(rng),
                         &rx, &ry, &area))
        return false;                       /* outside the image circle */

    vec3 rear = v3(rx, ry, c->pupil.rear_z_mm);
    vec3 d = v3sub(rear, film);
    ls_real Z = -d.z;                        /* axial gap, film to rear plane */
    if (!(Z > 0.0)) return false;
    ls_real dist = v3len(d);
    ls_real cos_theta = Z / dist;

    OsLensRay r = { film, v3scale(d, 1.0 / dist) };
    ls_real transmittance = 1.0;
    if (!os_lens_trace_reverse(L, wl.lambda_nm, &r, &transmittance))
        return false;                        /* vignetted; the caller counts it */

    ls_real c2 = cos_theta * cos_theta;
    out->weight = area * (c2 * c2) / (Z * Z) * transmittance * wl.inv_pdf;

    out->ray.o    = lens_point_to_world(c, r.o);
    out->ray.d    = v3norm(lens_dir_to_world(c, r.d));
    out->ray.tmin = 0.0;
    out->ray.tmax = HUGE_VAL;
    return true;
}

bool os_camera_project(const OsCamera *c, vec3 world, ls_real *px, ls_real *py) {
    const OsLens *L = &c->lens;

    /* World to lens space, millimetres. +z in lens space runs toward the
     * sensor, so a subject in front of the camera has NEGATIVE z. */
    vec3 off = v3scale(v3sub(world, c->eye), MM_PER_M);
    ls_real lx = v3dot(off, c->right);
    ls_real ly = v3dot(off, c->up);
    ls_real dist = -(-v3dot(off, c->fwd));      /* mm in front of the vertex */
    if (!(dist > 1.0)) return false;            /* at or behind the lens */

    /* Paraxial conjugate, from the principal planes -- the same expression
     * os_lens_focus uses, so the projection agrees with where the film is. */
    ls_real s_from_pp = dist - (L->ffd_mm + L->efl_mm);
    if (s_from_pp <= L->efl_mm) return false;
    ls_real s_prime = 1.0 / (1.0 / L->efl_mm - 1.0 / s_from_pp);
    ls_real m = s_prime / dist;

    /* The lens inverts, hence the negation on both axes. */
    ls_real fx = -lx * m;
    ls_real fy = -ly * m;

    /* Film millimetres back to pixels, inverting os_camera_sample exactly:
     *   fx = -(px/W - 0.5) * sensor_w
     *   fy =  (py/H - 0.5) * sensor_h                                        */
    *px = (0.5 - fx / c->sensor_w_mm) * (ls_real)c->width;
    *py = (fy / c->sensor_h_mm + 0.5) * (ls_real)c->height;
    return true;
}
