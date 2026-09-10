/* scene3d.c — the camera and its subjects in space. See scene3d.h. */
#include "scene3d.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void s3_init(Scene3D *s) {
    memset(s, 0, sizeof *s);
    /* A three-quarter view from the right and slightly above, framed on the
     * near half of the rail. Looking straight down the axis hides exactly what
     * this view is for -- how far apart things are -- and looking straight
     * across it hides the frustum. */
    /* Framed to hold the near half of the rail AND a key light overhead: a
     * lamp at head height is part of what this view is for, and a framing that
     * cuts it off makes the light controls feel like they act on nothing. */
    s->target = v3(0.30, 0.30, -2.1);
    s->dist   = 7.0;
    s->az     = 1.02;
    s->el     = 0.26;
}

static void seg(Scene3D *s, vec3 a, vec3 b, S3Kind k) {
    if (s->nseg >= S3_MAX_SEG) return;
    s->seg[s->nseg].a = a;
    s->seg[s->nseg].b = b;
    s->seg[s->nseg].kind = k;
    s->nseg++;
}

static void label(Scene3D *s, vec3 at, S3Kind k, const char *fmt, ...) {
    if (s->nlabel >= S3_MAX_LABEL) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s->label[s->nlabel].text, sizeof s->label[0].text, fmt, ap);
    va_end(ap);
    s->label[s->nlabel].at = at;
    s->label[s->nlabel].kind = k;
    s->nlabel++;
}

/* A circle of `n` chords about `c`, spanned by two perpendicular axes. Three
 * of these make a readable wireframe sphere; a solid one would hide the
 * objects behind it, which is the opposite of what this view is for. */
static void ring(Scene3D *s, vec3 c, vec3 u, vec3 v, ls_real r, int n, S3Kind k) {
    vec3 prev = v3add(c, v3scale(u, r));
    for (int i = 1; i <= n; ++i) {
        ls_real t = LS_TWO_PI * (ls_real)i / (ls_real)n;
        vec3 p = v3add(c, v3add(v3scale(u, r * cos(t)), v3scale(v, r * sin(t))));
        seg(s, prev, p, k);
        prev = p;
    }
}

static void sphere(Scene3D *s, vec3 c, ls_real r, S3Kind k) {
    ring(s, c, v3(1,0,0), v3(0,0,1), r, 20, k);   /* horizontal */
    ring(s, c, v3(1,0,0), v3(0,1,0), r, 20, k);
    ring(s, c, v3(0,1,0), v3(0,0,1), r, 20, k);
}

/* A rectangle perpendicular to the view axis at distance d, sized to what the
 * sensor sees there -- so a plane's WIDTH is the field of view, drawn to scale
 * rather than annotated. */
static void plane_at(Scene3D *s, ls_real d, ls_real half_w, ls_real half_h,
                     S3Kind k) {
    vec3 a = v3(-half_w, -half_h, -d), b = v3( half_w, -half_h, -d);
    vec3 c = v3( half_w,  half_h, -d), e = v3(-half_w,  half_h, -d);
    seg(s, a, b, k); seg(s, b, c, k); seg(s, c, e, k); seg(s, e, a, k);
}

void s3_build(Scene3D *s, const OsSceneDesc *d, const OsLens *L,
              ls_real sensor_w_mm, ls_real sensor_h_mm, ls_real coc_limit_mm,
              int sel_obj, int sel_light) {
    s->nseg = 0;
    s->nlabel = 0;
    s->near_m = s->far_m = s->hyperfocal_m = 0.0;

    /* How far out to draw. Governed by the furthest thing that matters, so the
     * view frames itself instead of needing a zoom every time the scene or the
     * focus changes. */
    ls_real reach = 8.0;
    for (int i = 0; i < d->nobj; ++i)
        if (d->obj[i].alive && d->obj[i].kind == OS_OBJ_SPHERE
            && -d->obj[i].centre.z > reach) reach = -d->obj[i].centre.z;
    if (reach > 14.0) reach = 14.0;

    /* ---- the ground, ruled every metre ----
     * Kept narrow. A wide floor is mostly empty and pulls the eye away from
     * the thing being explained. */
    ls_real ground = -0.75;
    ls_real halfw = 1.6;
    ls_real grid_reach = reach > 8.0 ? 8.0 : reach;
    for (ls_real x = -halfw; x <= halfw + 0.001; x += 0.8)
        seg(s, v3(x, ground, 0.4), v3(x, ground, -grid_reach), S3_GRID);
    for (ls_real z = 0.0; z <= grid_reach + 0.001; z += 1.0) {
        seg(s, v3(-halfw, ground, -z), v3(halfw, ground, -z), S3_GRID);
        if (z > 0.5 && fmod(z, 2.0) < 0.01)
            label(s, v3(-halfw - 0.12, ground, -z), S3_GRID, "%.0fM", (double)z);
    }

    seg(s, v3(0, 0, 0), v3(0, 0, -reach), S3_AXIS);

    /* ---- the camera ---- */
    ls_real bw = 0.09, bh = 0.065, bd = 0.10;
    vec3 c000 = v3(-bw, -bh, bd), c100 = v3(bw, -bh, bd);
    vec3 c110 = v3(bw,  bh, bd),  c010 = v3(-bw, bh, bd);
    vec3 c001 = v3(-bw, -bh, 0),  c101 = v3(bw, -bh, 0);
    vec3 c111 = v3(bw,  bh, 0),   c011 = v3(-bw, bh, 0);
    seg(s, c000, c100, S3_CAMERA); seg(s, c100, c110, S3_CAMERA);
    seg(s, c110, c010, S3_CAMERA); seg(s, c010, c000, S3_CAMERA);
    seg(s, c001, c101, S3_CAMERA); seg(s, c101, c111, S3_CAMERA);
    seg(s, c111, c011, S3_CAMERA); seg(s, c011, c001, S3_CAMERA);
    seg(s, c000, c001, S3_CAMERA); seg(s, c100, c101, S3_CAMERA);
    seg(s, c110, c111, S3_CAMERA); seg(s, c010, c011, S3_CAMERA);
    label(s, v3(0.0, bh + 0.16, 0.0), S3_CAMERA, "CAMERA");

    ls_real barrel_r = L ? (L->ep_semi_ap_mm * 0.001 + 0.012) : 0.03;
    ls_real barrel_l = L ? (L->total_track_mm + L->efl_mm) * 0.001 * 0.35 : 0.06;
    if (barrel_l < 0.03) barrel_l = 0.03;
    ring(s, v3(0, 0, 0.0),       v3(1,0,0), v3(0,1,0), barrel_r, 16, S3_CAMERA);
    ring(s, v3(0, 0, -barrel_l), v3(1,0,0), v3(0,1,0), barrel_r, 16, S3_CAMERA);
    for (int i = 0; i < 4; ++i) {
        ls_real t = LS_TWO_PI * (ls_real)i / 4.0;
        vec3 o = v3(barrel_r * cos(t), barrel_r * sin(t), 0.0);
        seg(s, o, v3(o.x, o.y, -barrel_l), S3_CAMERA);
    }

    if (!L) return;

    /* ---- what the sensor can see ---- */
    ls_real tan_h = sensor_w_mm * 0.5 / L->efl_mm;
    ls_real tan_v = sensor_h_mm * 0.5 / L->efl_mm;
    plane_at(s, reach, reach * tan_h, reach * tan_v, S3_FRUSTUM);
    for (int i = 0; i < 4; ++i) {
        ls_real sx = (i == 0 || i == 3) ? -1.0 : 1.0;
        ls_real sy = (i < 2) ? -1.0 : 1.0;
        seg(s, v3(0, 0, 0),
            v3(sx * reach * tan_h, sy * reach * tan_v, -reach), S3_FRUSTUM);
    }

    /* ---- focus, and the slab either side of it that counts as sharp ---- */
    ls_real f = L->focus_distance_m;
    if (isfinite(f) && f > 0.0 && f <= reach) {
        plane_at(s, f, f * tan_h, f * tan_v, S3_FOCUS);
        label(s, v3(f * tan_h * 1.08, f * tan_v * 0.9, -f),
              S3_FOCUS, "FOCUS %.2fM", f);
    }

    ls_real nr = 0.0, fr = 0.0;
    bool have_dof = os_lens_dof(L, coc_limit_mm, &nr, &fr);
    if (have_dof) {
        s->near_m = nr;
        s->far_m  = fr;
        s->hyperfocal_m = os_lens_hyperfocal_m(L, coc_limit_mm);

        /* The three planes crowd together whenever the depth of field is
         * shallow, so their labels are staggered vertically -- stacked on one
         * line they overprint exactly when the numbers matter most. */
        if (nr > 0.02 && nr <= reach) {
            plane_at(s, nr, nr * tan_h, nr * tan_v, S3_DOF);
            label(s, v3(-nr * tan_h * 1.08, -nr * tan_v * 0.9, -nr),
                  S3_DOF, "NEAR %.2fM", nr);
        }
        if (isfinite(fr) && fr <= reach) {
            plane_at(s, fr, fr * tan_h, fr * tan_v, S3_DOF);
            label(s, v3(-fr * tan_h * 1.08, fr * tan_v * 0.9, -fr),
                  S3_DOF, "FAR %.2fM", fr);
        }
        if (nr > 0.02 && isfinite(fr) && fr <= reach) {
            for (int i = 0; i < 4; ++i) {
                ls_real sx = (i == 0 || i == 3) ? -1.0 : 1.0;
                ls_real sy = (i < 2) ? -1.0 : 1.0;
                seg(s, v3(sx * nr * tan_h, sy * nr * tan_v, -nr),
                       v3(sx * fr * tan_h, sy * fr * tan_v, -fr), S3_DOF);
            }
        }
    }

    /* ---- the subjects, at the distances the description says ---- */
    for (int i = 0; i < d->nobj; ++i) {
        const OsObject *o = &d->obj[i];
        if (!o->alive || o->kind != OS_OBJ_SPHERE) continue;

        /* Marked means INSIDE THE DEPTH OF FIELD: between the near and far
         * limits the same os_lens_dof draws its slab at. Every subject in the
         * slab is marked, on axis or not -- the slab and the marks are then
         * one statement rather than two, and a subject sitting visibly inside
         * the drawn planes can never come out unmarked.
         *
         * This is defocus only, which is what depth of field has always
         * meant. It is NOT the whole of how sharp a subject looks: off axis an
         * uncorrected doublet adds coma and astigmatism that no
         * depth-of-field formula knows about, so a marked subject near the
         * frame edge can still be soft in the render. os_lens_spot_mm traces
         * that real spot, and the panel reports it for the selected subject --
         * the honest number is one click away rather than silently overriding
         * what the slab says. */
        ls_real dist = -o->centre.z;
        bool sharp = have_dof && dist >= nr && dist <= fr;
        bool sel = (i == sel_obj);
        sphere(s, o->centre, o->radius, sel ? S3_SELECTED
                                            : (sharp ? S3_SUBJECT : S3_OBJECT));
        /* A dropped line to the ground: a sphere floating in a perspective
         * view has no readable depth on its own. */
        seg(s, o->centre, v3(o->centre.x, ground, o->centre.z), S3_GRID);

        if (sel) {
            /* A surrounding box rather than a recolour, so "selected" and "in
             * focus" can both be true without one hiding the other. */
            ls_real e = o->radius * 1.45;
            vec3 c = o->centre;
            for (int a = 0; a < 3; ++a) {
                vec3 u = (a == 0) ? v3(1,0,0) : (a == 1) ? v3(0,1,0) : v3(0,0,1);
                vec3 w = (a == 0) ? v3(0,1,0) : (a == 1) ? v3(0,0,1) : v3(1,0,0);
                for (int k = -1; k <= 1; k += 2)
                    for (int m = -1; m <= 1; m += 2) {
                        vec3 p0 = v3add(c, v3add(v3scale(u, e * k), v3scale(w, e * m)));
                        vec3 ax = (a == 0) ? v3(0,0,1) : (a == 1) ? v3(1,0,0) : v3(0,1,0);
                        seg(s, v3add(p0, v3scale(ax, -e)),
                               v3add(p0, v3scale(ax,  e)), S3_SELECTED);
                    }
            }
        }
        label(s, v3(o->centre.x, o->centre.y + o->radius + 0.14, o->centre.z),
              sel ? S3_SELECTED : (sharp ? S3_SUBJECT : S3_OBJECT),
              "%s", o->name);
    }

    /* ---- the lamps ---- */
    for (int i = 0; i < d->nlit; ++i) {
        const OsLight *l = &d->lit[i];
        if (!l->alive) continue;
        bool sel = (i == sel_light);
        /* Under AMBIENT the lamps emit nothing, so they are drawn as ordinary
         * objects rather than as sources. Still drawn, and still draggable:
         * losing them off the screen would make switching modes feel like
         * deleting them. */
        bool off = (d->light_mode == OS_LIGHT_AMBIENT);
        S3Kind k = sel ? S3_SELECTED : (off ? S3_OBJECT : S3_LIGHT);

        if (l->kind == OS_LIGHT_RECT) {
            ls_real u = l->size_u * 0.5, w = l->size_v * 0.5;
            vec3 a = v3(l->centre.x - u, l->centre.y, l->centre.z - w);
            vec3 b = v3(l->centre.x + u, l->centre.y, l->centre.z - w);
            vec3 c = v3(l->centre.x + u, l->centre.y, l->centre.z + w);
            vec3 e = v3(l->centre.x - u, l->centre.y, l->centre.z + w);
            seg(s, a, b, k); seg(s, b, c, k); seg(s, c, e, k); seg(s, e, a, k);
            /* An X across it, so a rect lamp seen edge-on is still visible. */
            seg(s, a, c, k); seg(s, b, e, k);
        } else {
            ring(s, l->centre, v3(1,0,0), v3(0,0,1), l->radius, 14, k);
            ring(s, l->centre, v3(1,0,0), v3(0,1,0), l->radius, 14, k);
        }
        /* Rays leaving it, so a lamp reads as a source rather than as an
         * object. Length is fixed rather than scaled by flux: a 20000 lm lamp
         * would otherwise fill the view. */
        for (int r = 0; r < 6; ++r) {
            ls_real t = LS_TWO_PI * (ls_real)r / 6.0;
            vec3 dir = v3(cos(t) * 0.7, -0.7, sin(t) * 0.7);
            ls_real r0 = (l->kind == OS_LIGHT_RECT) ? 0.0 : l->radius;
            seg(s, v3add(l->centre, v3scale(dir, r0)),
                   v3add(l->centre, v3scale(dir, r0 + 0.16)), k);
        }
        seg(s, l->centre, v3(l->centre.x, ground, l->centre.z), S3_GRID);
        if (off)
            label(s, v3(l->centre.x, l->centre.y + 0.20, l->centre.z), k,
                  "%s OFF", l->name);
        else
            label(s, v3(l->centre.x, l->centre.y + 0.20, l->centre.z), k,
                  "%s %.0fLM", l->name, (double)l->flux_lm);
    }

    /* ---- the dome ----
     *
     * Drawn as strokes coming INWARD from every direction, not as a surface.
     * A uniform sky is infinitely far away, so any drawn radius is a lie
     * about a distance -- and a wireframe hemisphere at a plausible radius
     * reads as a wall around the set, which is the one thing it is not. The
     * strokes say the only true thing there is to say: light arrives from
     * everywhere, from no particular place.
     *
     * Two faint latitude rings, and no meridians. The meridians were the
     * lines that ran off the edge of the canvas and turned the diagram into
     * a cage.
     */
    if (d->light_mode == OS_LIGHT_AMBIENT) {
        vec3    c = v3(0.0, ground + 0.7, -grid_reach * 0.42);
        ls_real R = 2.9;

        for (int i = 0; i < 2; ++i) {
            ls_real el = 0.30 + 0.55 * (ls_real)i;
            ring(s, v3(c.x, c.y + R * sin(el), c.z), v3(1,0,0), v3(0,0,1),
                 R * cos(el), 32, S3_SKY);
        }
        /* Arriving light. Elevations stepped by a coprime stride so the
         * strokes do not line up into a band at one height. */
        for (int i = 0; i < 16; ++i) {
            ls_real t  = LS_TWO_PI * (ls_real)i / 16.0;
            ls_real el = 0.12 + 0.30 * (ls_real)((i * 7) % 5);
            vec3 dir = v3(cos(el) * cos(t), sin(el), cos(el) * sin(t));
            vec3 p0 = v3add(c, v3scale(dir, R));
            vec3 p1 = v3add(c, v3scale(dir, R - 0.42));
            seg(s, p0, p1, S3_SKY);
            /* A head on the inward end, so it reads as arriving rather than
             * as leaving. */
            vec3 side = v3norm(v3cross(dir, v3(0,1,0)));
            seg(s, p1, v3add(v3add(p1, v3scale(dir, 0.16)),
                             v3scale(side,  0.07)), S3_SKY);
            seg(s, p1, v3add(v3add(p1, v3scale(dir, 0.16)),
                             v3scale(side, -0.07)), S3_SKY);
        }
        /* On the lower ring rather than over the pole: the pole is off the
         * top of the canvas at the default framing, and a label you have to
         * orbit to find is not a label. */
        label(s, v3(c.x + R * cos(0.30) * 0.72, c.y + R * sin(0.30),
                    c.z + R * cos(0.30) * 0.72), S3_SKY,
              "SKY %.0f LX", (double)d->ambient_lux);
    }
}

/* ---- picking and dragging ---- */

void s3_pick(const Scene3D *s, const OsSceneDesc *d, int w, int h,
             ls_real px, ls_real py, int *obj, int *light) {
    *obj = -1;
    *light = -1;
    ls_real best = S3_PICK_PX * S3_PICK_PX;

    /* Lights are tested LAST with `<=`, so they win ties: they are drawn
     * smaller and are the harder thing to hit. */
    for (int i = 0; i < d->nobj; ++i) {
        if (!d->obj[i].alive || d->obj[i].kind != OS_OBJ_SPHERE) continue;
        ls_real sx, sy;
        if (!s3_project(s, d->obj[i].centre, w, h, &sx, &sy)) continue;
        ls_real dx = sx - px, dy = sy - py, d2 = dx * dx + dy * dy;
        if (d2 <= best) { best = d2; *obj = i; *light = -1; }
    }
    for (int i = 0; i < d->nlit; ++i) {
        if (!d->lit[i].alive) continue;
        ls_real sx, sy;
        if (!s3_project(s, d->lit[i].centre, w, h, &sx, &sy)) continue;
        ls_real dx = sx - px, dy = sy - py, d2 = dx * dx + dy * dy;
        if (d2 <= best) { best = d2; *light = i; *obj = -1; }
    }
}

/* ---- the orbit camera ---- */

static void s3_basis(const Scene3D *s, vec3 *eye, vec3 *fwd, vec3 *right, vec3 *up) {
    vec3 e = v3(s->target.x + s->dist * cos(s->el) * sin(s->az),
                s->target.y + s->dist * sin(s->el),
                s->target.z + s->dist * cos(s->el) * cos(s->az));
    vec3 f = v3norm(v3sub(s->target, e));
    vec3 r = v3norm(v3cross(f, v3(0, 1, 0)));
    *eye = e; *fwd = f; *right = r;
    *up = v3cross(r, f);
}

bool s3_project(const Scene3D *s, vec3 p, int w, int h, ls_real *sx, ls_real *sy) {
    vec3 eye, fwd, right, up;
    s3_basis(s, &eye, &fwd, &right, &up);

    vec3 d = v3sub(p, eye);
    ls_real z = v3dot(d, fwd);
    if (z <= 1e-4) return false;              /* at or behind the eye */

    /* A fixed 42-degree view: wide enough to hold a 14 m rail without the near
     * objects ballooning, which a shorter one does. */
    ls_real half = tan(0.5 * 42.0 * LS_PI / 180.0);
    ls_real aspect = (ls_real)w / (ls_real)h;
    ls_real nx = v3dot(d, right) / (z * half * aspect);
    ls_real ny = v3dot(d, up)    / (z * half);
    *sx = (nx + 1.0) * 0.5 * (ls_real)w;
    *sy = (1.0 - ny) * 0.5 * (ls_real)h;
    return true;
}

void s3_pick_ray(const Scene3D *s, int w, int h, ls_real px, ls_real py,
                 vec3 *o, vec3 *dir) {
    vec3 eye, fwd, right, up;
    s3_basis(s, &eye, &fwd, &right, &up);

    /* The exact inverse of s3_project's mapping. Written from the same two
     * lines rather than re-derived: an unprojection that disagrees with the
     * projection by a sign produces a drag that moves the wrong way, which
     * reads as a broken interface rather than as wrong arithmetic. */
    ls_real half = tan(0.5 * 42.0 * LS_PI / 180.0);
    ls_real aspect = (ls_real)w / (ls_real)h;
    ls_real nx = (2.0 * px / (ls_real)w - 1.0) * half * aspect;
    ls_real ny = (1.0 - 2.0 * py / (ls_real)h) * half;

    *o = eye;
    *dir = v3norm(v3add(fwd, v3add(v3scale(right, nx), v3scale(up, ny))));
}

bool s3_plane_hit(const Scene3D *s, int w, int h, ls_real px, ls_real py,
                  ls_real plane_y, vec3 *out) {
    vec3 o, dir;
    s3_pick_ray(s, w, h, px, py, &o, &dir);
    /* Looking along the plane, every point on it is equally under the cursor,
     * so there is no answer -- refusing is better than returning a point
     * thousands of metres away. */
    if (fabs(dir.y) < 1e-6) return false;
    ls_real t = (plane_y - o.y) / dir.y;
    if (t <= 0.0) return false;              /* the plane is behind the eye */
    *out = v3add(o, v3scale(dir, t));
    return true;
}

void s3_orbit(Scene3D *s, ls_real d_az, ls_real d_el) {
    s->az += d_az;
    s->el += d_el;
    /* Stopped just short of the poles. Straight overhead the up vector becomes
     * parallel to the view direction, the basis degenerates and the whole
     * scene flips over -- which reads as a bug, not as a viewpoint. */
    const ls_real LIMIT = 1.45;
    if (s->el >  LIMIT) s->el =  LIMIT;
    if (s->el < -LIMIT) s->el = -LIMIT;
}

void s3_zoom(Scene3D *s, ls_real factor) {
    s->dist *= factor;
    if (s->dist < 0.6)  s->dist = 0.6;
    if (s->dist > 60.0) s->dist = 60.0;
}
