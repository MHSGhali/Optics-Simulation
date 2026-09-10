/* inspect.c — the field list and the edit semantics. See inspect.h. */
#include "inspect.h"

#include "lightsim/units.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *const LENS_NAMES[]  = { "IDEAL", "SINGLET", "ACHROMAT" };
static const char *const STAGE_NAMES[] = { "RAIL", "BOKEH" };
static const char *const OBJ_NAMES[]   = { "SPHERE", "PLANE" };
static const char *const LIT_NAMES[]   = { "SPHERE", "RECT" };
static const char *const MODE_NAMES[]  = { "LAMPS", "AMBIENT" };

void os_settings_default(OsSettings *s) {
    memset(s, 0, sizeof *s);
    s->lens        = OS_LENS_ACHROMAT_100;
    s->focal_mm    = 100.0;
    s->fno         = 5.0;
    s->focus_m     = 2.0;
    s->blades      = 0;
    s->curvature   = 0.0;
    s->rot_deg     = 0.0;
    s->stage       = OS_STAGE_DEPTH_RAIL;
    os_scenedesc_preset(&s->scene, OS_STAGE_DEPTH_RAIL);
    s->sel_obj     = -1;
    s->sel_light   = -1;
    s->sensor_w_mm = 36.0;
    s->res_w       = 320;
    s->exposure    = 100.0;
    /* The circle of confusion that still reads as sharp. 0.030 mm on a 36 mm
     * frame is the number every depth-of-field table has used for a century --
     * it is roughly what the eye resolves in an 8x10 print at arm's length. */
    s->coc_limit_mm = 0.030;
    s->spp         = 4;
    s->depth       = 5;
    s->view        = OS_VIEW_LENS;
    s->show_rays   = true;
    s->show_grid   = true;
}

int os_settings_res_h(const OsSettings *s) {
    /* 3:2, the aspect of the 36 x 24 mm format the sensor width names. Derived
     * rather than stored so the render grid and the sensor cannot disagree. */
    int h = (int)((double)s->res_w * 2.0 / 3.0 + 0.5);
    return h < 2 ? 2 : h;
}

bool os_settings_image_differs(const OsSettings *a, const OsSettings *b) {
    /* The arrangement is compared with memcmp, so a field added to OsObject or
     * OsLight cannot be forgotten here -- which is exactly the kind of omission
     * that leaves a control apparently doing nothing until something else is
     * touched. It is a plain POD with no padding worth worrying about because
     * every member is a double, an int or a small char array.
     *
     * Selection is NOT part of it: clicking an object must not throw away a
     * converged render. */
    if (memcmp(&a->scene, &b->scene, sizeof a->scene) != 0) return true;

    /* The rest is spelled out rather than memcmp'd, because the view toggles
     * live in the same struct and must not restart anything. Turning the ray
     * fan off is not a change to the photograph. */
    return a->lens != b->lens
        || a->focal_mm != b->focal_mm
        || a->fno != b->fno
        || a->focus_m != b->focus_m
        || a->blades != b->blades
        || a->curvature != b->curvature
        || a->rot_deg != b->rot_deg
        || a->stage != b->stage
        || a->sensor_w_mm != b->sensor_w_mm
        || a->res_w != b->res_w
        || a->spp != b->spp
        || a->depth != b->depth;
}

bool os_settings_doc_differs(const OsSettings *a, const OsSettings *b) {
    /* Everything the image depends on, plus the two that change what the
     * program SAYS about the image without changing the image itself.
     * Exposure is a view gain and the sharpness criterion only moves the
     * depth-of-field numbers -- neither restarts a render, and both are
     * edits a person would expect to be able to take back. */
    return os_settings_image_differs(a, b)
        || a->exposure     != b->exposure
        || a->coc_limit_mm != b->coc_limit_mm
        /* Selection, which os_settings_image_differs deliberately ignores.
         * On its own it is not an edit -- but deleting an object changes it,
         * and an undo that brought the object back without reselecting it
         * would leave the panel editing nothing. */
        || a->sel_obj      != b->sel_obj
        || a->sel_light    != b->sel_light;
}

void os_settings_restore_doc(OsSettings *dst, const OsSettings *src) {
    int  view = dst->view;
    bool rays = dst->show_rays, spot = dst->show_spot;
    bool grid = dst->show_grid, chrom = dst->chromatic;
    *dst = *src;
    dst->view = view;
    dst->show_rays = rays; dst->show_spot = spot;
    dst->show_grid = grid; dst->chromatic = chrom;
}

/* ---- the field list ---- */

static Field head(FieldId id, const char *label) {
    Field f; memset(&f, 0, sizeof f);
    f.id = id; f.label = label; f.unit = ""; f.heading = true; f.readonly = true;
    return f;
}
static Field val(FieldId id, const char *label, const char *unit,
                 double v, double lo, double hi, bool logarithmic) {
    Field f; memset(&f, 0, sizeof f);
    f.id = id; f.label = label; f.unit = unit;
    f.value = v; f.lo = lo; f.hi = hi; f.logarithmic = logarithmic;
    return f;
}
static Field ival(FieldId id, const char *label, const char *unit,
                  double v, double lo, double hi, bool logarithmic) {
    Field f = val(id, label, unit, v, lo, hi, logarithmic);
    f.integral = true;
    return f;
}
static Field ro(FieldId id, const char *label, const char *unit, double v) {
    Field f; memset(&f, 0, sizeof f);
    f.id = id; f.label = label; f.unit = unit; f.value = v; f.readonly = true;
    return f;
}
static Field en(FieldId id, const char *label, double v,
                const char *const *names, int n) {
    Field f; memset(&f, 0, sizeof f);
    f.id = id; f.label = label; f.unit = "";
    f.value = v; f.lo = 0; f.hi = n - 1; f.is_enum = true;
    f.names = names; f.nnames = n;
    return f;
}

int os_inspect_fields(const OsSettings *s, const OsLens *L,
                      uint64_t samples_done, Field *out, int max) {
    int n = 0;
    #define PUSH(f) do { if (n < max) out[n++] = (f); } while (0)

    PUSH(head(FLD_H_LENS, "LENS"));
    PUSH(en(FLD_LENS, "DESIGN", s->lens, LENS_NAMES, OS_LENS_COUNT));
    PUSH(val(FLD_FOCAL,  "FOCAL",  "MM", s->focal_mm, 12.0, 400.0, true));
    PUSH(val(FLD_FNO,    "APERTURE", "F/", s->fno, 1.0, 45.0, true));
    PUSH(val(FLD_FOCUS,  "FOCUS",  "M",  s->focus_m, 0.15, 1000.0, true));
    PUSH(ival(FLD_BLADES, "BLADES", "",  s->blades, 0, 14, false));
    PUSH(val(FLD_CURVE,  "BLADE CURVE", "", s->curvature, 0.0, 1.0, false));
    PUSH(val(FLD_ROT,    "BLADE ANGLE", "DEG", s->rot_deg, 0.0, 90.0, false));

    PUSH(head(FLD_H_SCENE, "SCENE"));
    PUSH(en(FLD_STAGE, "PRESET", s->stage, STAGE_NAMES, OS_STAGE_COUNT));
    PUSH(en(FLD_LIGHTING, "LIGHTING", s->scene.light_mode,
            MODE_NAMES, OS_LIGHT_MODE_COUNT));
    /* The dome's own controls appear only when the dome is what is lighting
     * the scene. Showing a lux figure next to lamps that are actually doing
     * the work would be two answers to one question. */
    if (s->scene.light_mode == OS_LIGHT_AMBIENT) {
        /* Illuminance on a surface facing the sky -- what a light meter aimed
         * upward reads, and the number an exposure is chosen from. */
        PUSH(val(FLD_AMB_LUX, "AMBIENT", "LX", s->scene.ambient_lux,
                 0.0, OS_AMBIENT_MAX_LX, true));
        PUSH(val(FLD_AMB_CCT, "SKY COLOUR", "K", s->scene.ambient_cct_k,
                 OS_CCT_MIN_K, OS_CCT_MAX_K, true));
    }

    /* The selected subject, if any. Shown only when something is selected, so
     * the panel is about what you are editing rather than about everything. */
    if (s->sel_obj >= 0 && s->sel_obj < s->scene.nobj
        && s->scene.obj[s->sel_obj].alive) {
        const OsObject *o = &s->scene.obj[s->sel_obj];
        PUSH(head(FLD_H_OBJECT, o->name[0] ? o->name : "OBJECT"));
        PUSH(en(FLD_O_KIND, "SHAPE", o->kind, OBJ_NAMES, OS_OBJ_KIND_COUNT));
        PUSH(val(FLD_O_X, "X", "M", o->centre.x, -OS_POS_LIMIT_M, OS_POS_LIMIT_M, false));
        PUSH(val(FLD_O_Y, "Y", "M", o->centre.y, -OS_POS_LIMIT_M, OS_POS_LIMIT_M, false));
        PUSH(val(FLD_O_Z, "Z", "M", o->centre.z, -OS_POS_LIMIT_M, OS_POS_LIMIT_M, false));
        if (o->kind == OS_OBJ_SPHERE)
            PUSH(val(FLD_O_RADIUS, "RADIUS", "M", o->radius,
                     OS_SIZE_MIN_M, OS_SIZE_MAX_M, true));
        PUSH(val(FLD_O_R, "RED",   "", o->rgb[0], 0.0, 1.0, false));
        PUSH(val(FLD_O_G, "GREEN", "", o->rgb[1], 0.0, 1.0, false));
        PUSH(val(FLD_O_B, "BLUE",  "", o->rgb[2], 0.0, 1.0, false));
        /* The number the whole stage exists to make knowable. */
        PUSH(ro(FLD_O_DEPTH, "DEPTH", "M", os_scenedesc_depth(&s->scene, s->sel_obj)));
        /* And what it ACTUALLY costs in sharpness, traced at this subject's
         * own field position rather than read off an on-axis band. */
        if (L) {
            ls_real hgt = sqrt(o->centre.x * o->centre.x + o->centre.y * o->centre.y);
            PUSH(ro(FLD_O_SPOT, "SPOT", "MM",
                    os_lens_spot_mm(L, -o->centre.z, hgt, 13)));
        }
    }

    if (s->sel_light >= 0 && s->sel_light < s->scene.nlit
        && s->scene.lit[s->sel_light].alive) {
        const OsLight *l = &s->scene.lit[s->sel_light];
        PUSH(head(FLD_H_LIGHT, l->name[0] ? l->name : "LIGHT"));
        PUSH(en(FLD_L_KIND, "SHAPE", l->kind, LIT_NAMES, OS_LIGHT_KIND_COUNT));
        PUSH(val(FLD_L_X, "X", "M", l->centre.x, -OS_POS_LIMIT_M, OS_POS_LIMIT_M, false));
        PUSH(val(FLD_L_Y, "Y", "M", l->centre.y, -OS_POS_LIMIT_M, OS_POS_LIMIT_M, false));
        PUSH(val(FLD_L_Z, "Z", "M", l->centre.z, -OS_POS_LIMIT_M, OS_POS_LIMIT_M, false));
        if (l->kind == OS_LIGHT_RECT) {
            PUSH(val(FLD_L_SIZEU, "WIDTH",  "M", l->size_u,
                     OS_SIZE_MIN_M, OS_SIZE_MAX_M, true));
            PUSH(val(FLD_L_SIZEV, "DEPTH",  "M", l->size_v,
                     OS_SIZE_MIN_M, OS_SIZE_MAX_M, true));
        } else {
            PUSH(val(FLD_L_RADIUS, "RADIUS", "M", l->radius,
                     OS_SIZE_MIN_M, OS_SIZE_MAX_M, true));
        }
        /* Authored in lumens, the number printed on a real lamp. Watts follow
         * from it and the colour temperature; changing the colour leaves the
         * lumens where they were, which is what anyone adjusting a colour
         * temperature means. */
        PUSH(val(FLD_L_FLUX, "FLUX", "LM", l->flux_lm, 0.0, OS_FLUX_MAX_LM, true));
        PUSH(val(FLD_L_CCT,  "COLOUR", "K", l->cct_k,
                 OS_CCT_MIN_K, OS_CCT_MAX_K, true));
        {
            Spectrum spd = ls_spectrum_blackbody(l->cct_k);
            ls_real w = ls_watts_from_lumens(l->flux_lm, &spd);
            PUSH(ro(FLD_L_WATTS, "RADIANT", "W", w));
            /* IN-BAND efficacy. units.h cautions that the literature quotes
             * this against TOTAL radiant power, which for a blackbody differs
             * enormously -- 121.6 against 16.4 lm/W at 2856 K -- so the label
             * says which one this is. */
            PUSH(ro(FLD_L_EFFICACY, "LM/W IN BAND", "",
                    ls_luminous_efficacy_band(&spd)));
        }
    }

    PUSH(head(FLD_H_SENSOR, "SENSOR"));
    PUSH(val(FLD_SENSOR_W, "WIDTH",    "MM", s->sensor_w_mm, 4.0, 80.0, true));
    PUSH(ival(FLD_RES,     "RENDER",   "PX", s->res_w, 64, 1600, true));
    PUSH(val(FLD_EXPOSURE, "EXPOSURE", "X",  s->exposure, 1e-4, 1e6, true));
    PUSH(val(FLD_COC,      "SHARP IF",  "MM", s->coc_limit_mm, 0.002, 0.2, true));

    PUSH(head(FLD_H_SAMPLING, "SAMPLING"));
    PUSH(ival(FLD_SPP,  "PER PASS", "SPP", s->spp, 1, 256, true));
    PUSH(ival(FLD_DEPTH,"BOUNCES",  "",    s->depth, 1, 16, false));

    PUSH(head(FLD_H_DERIVED, "DERIVED"));
    if (L) {
        PUSH(ro(FLD_D_EFL,   "FOCAL",    "MM", L->efl_mm));
        PUSH(ro(FLD_D_HFOV,  "H FIELD",  "DEG",
                2.0 * atan2(s->sensor_w_mm * 0.5, L->efl_mm) * 180.0 / LS_PI));
        PUSH(ro(FLD_D_EP,    "PUPIL",    "MM", 2.0 * L->ep_semi_ap_mm));
        /* The f-stop / T-stop gap: real transmitted light, not geometry. */
        PUSH(ro(FLD_D_TSTOP, "T-STOP",   "T/",
                L->f_number / sqrt(os_lens_transmittance(L, OS_LINE_D))));
        PUSH(ro(FLD_D_BFD,   "BACK FOCUS", "MM", L->bfd_mm));
        PUSH(ro(FLD_D_FILM,  "FILM AT",  "MM", L->film_z_mm));
        /* Longitudinal chromatic aberration as a number: swap the singlet for
         * the achromat and watch it fall by a factor of twenty. */
        PUSH(ro(FLD_D_COLOUR, "COLOUR ERR", "%",
                100.0 * (os_lens_efl_at(L, OS_LINE_F)
                       - os_lens_efl_at(L, OS_LINE_C)) / L->efl_mm));
        PUSH(ro(FLD_D_COC,   "BLUR AT 6M", "MM", os_lens_coc_mm(L, 6.0)));
        PUSH(ro(FLD_D_COVER, "COVERS",   "MM", L->image_circle_mm));
        /* Where the sharp slab actually falls, solved from this lens rather
         * than from the textbook hyperfocal formula. */
        {
            ls_real nr = 0.0, fr = 0.0;
            if (os_lens_dof(L, s->coc_limit_mm, &nr, &fr)) {
                /* ON AXIS, and the label says so. These come from defocus
                 * alone; off axis an uncorrected doublet's coma and
                 * astigmatism can dominate them completely, and a subject
                 * inside this band can still be the blurriest thing in frame.
                 * The SPOT row above is the honest answer for a given
                 * subject. */
                PUSH(ro(FLD_D_NEAR, "AXIS SHARP FROM", "M", nr));
                PUSH(ro(FLD_D_FAR,  "AXIS SHARP TO",   "M", fr));
            }
            PUSH(ro(FLD_D_HYPER, "AXIS HYPERFOC", "M",
                    os_lens_hyperfocal_m(L, s->coc_limit_mm)));
        }
    }
    PUSH(ro(FLD_D_SAMPLES, "SAMPLES", "SPP", (double)samples_done));

    #undef PUSH
    return n;
}

/* ---- editing ---- */

static double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

bool os_inspect_set(OsSettings *s, FieldId id, double v) {
    /* A non-finite value is refused outright rather than clamped.
     *
     * Clamping does not catch it: both `v < lo` and `v > hi` are FALSE for a
     * NaN, so the obvious three-way clamp passes it straight through. And a
     * NaN focal length is not a visible error -- it propagates into every ray,
     * every intersection test fails, and the window simply goes black with
     * nothing to say why. Typing "-" or "." on its own is enough to produce
     * one through atof(), so this is a reachable input, not a theoretical one. */
    if (!isfinite(v)) return false;

    /* Every path -- typed, dragged, or driven by a toolbar button -- comes
     * through here, so a value that is impossible one way is impossible every
     * way. */
    switch (id) {
        case FLD_LENS: {
            int nv = (int)clampd(v, 0, OS_LENS_COUNT - 1);
            if (nv == s->lens) return false;
            s->lens = nv; return true;
        }
        case FLD_STAGE: {
            int nv = (int)clampd(v, 0, OS_STAGE_COUNT - 1);
            if (nv == s->stage) return false;
            /* A preset is a fresh scene, so choosing one DISCARDS the current
             * arrangement. That is what a preset should do; the caller says so
             * on the status line rather than letting it happen silently. */
            s->stage = nv;
            /* ...except the lighting MODE and the dome's settings, which
             * survive. Which preset you are looking at and how it is lit are
             * different questions, and having "ambient" silently revert when
             * you change subject is the kind of state loss that makes a
             * control feel unreliable. */
            OsLightMode  mode = s->scene.light_mode;
            ls_real      lux  = s->scene.ambient_lux;
            ls_real      cct  = s->scene.ambient_cct_k;
            os_scenedesc_preset(&s->scene, (OsStageId)nv);
            s->scene.light_mode    = mode;
            s->scene.ambient_lux   = lux;
            s->scene.ambient_cct_k = cct;
            s->sel_obj = s->sel_light = -1;
            return true;
        }
        case FLD_LIGHTING: {
            int nv = (int)clampd(v, 0, OS_LIGHT_MODE_COUNT - 1);
            if ((OsLightMode)nv == s->scene.light_mode) return false;
            s->scene.light_mode = (OsLightMode)nv;
            return true;
        }
        case FLD_AMB_LUX: {
            ls_real nv = clampd(v, 0.0, OS_AMBIENT_MAX_LX);
            if (nv == s->scene.ambient_lux) return false;
            s->scene.ambient_lux = nv; return true;
        }
        case FLD_AMB_CCT: {
            ls_real nv = clampd(v, OS_CCT_MIN_K, OS_CCT_MAX_K);
            if (nv == s->scene.ambient_cct_k) return false;
            s->scene.ambient_cct_k = nv; return true;
        }

        /* ---- the selected subject ---- */
        case FLD_O_KIND: case FLD_O_X: case FLD_O_Y: case FLD_O_Z:
        case FLD_O_RADIUS: case FLD_O_R: case FLD_O_G: case FLD_O_B: {
            if (s->sel_obj < 0 || s->sel_obj >= s->scene.nobj) return false;
            OsObject *o = &s->scene.obj[s->sel_obj];
            if (!o->alive) return false;
            OsObject before = *o;

            switch (id) {
                case FLD_O_KIND:
                    o->kind = (OsObjKind)(int)clampd(v, 0, OS_OBJ_KIND_COUNT - 1);
                    /* Re-homed unconditionally: radius means something for a
                     * sphere and nothing for a plane, and gating this on "only
                     * if unset" re-homes it the FIRST time only -- so cycling
                     * the kind twice leaves a number from the wrong quantity
                     * sitting in the field. */
                    os_scenedesc_rehome_object(o);
                    break;
                case FLD_O_X:      o->centre.x = v; break;
                case FLD_O_Y:      o->centre.y = v; break;
                case FLD_O_Z:      o->centre.z = v; break;
                case FLD_O_RADIUS: o->radius   = v; break;
                case FLD_O_R:      o->rgb[0]   = v; break;
                case FLD_O_G:      o->rgb[1]   = v; break;
                case FLD_O_B:      o->rgb[2]   = v; break;
                default: break;
            }
            os_scenedesc_clamp_object(o);
            return memcmp(&before, o, sizeof before) != 0;
        }

        /* ---- the selected light ---- */
        case FLD_L_KIND: case FLD_L_X: case FLD_L_Y: case FLD_L_Z:
        case FLD_L_RADIUS: case FLD_L_SIZEU: case FLD_L_SIZEV:
        case FLD_L_FLUX: case FLD_L_CCT: {
            if (s->sel_light < 0 || s->sel_light >= s->scene.nlit) return false;
            OsLight *l = &s->scene.lit[s->sel_light];
            if (!l->alive) return false;
            OsLight before = *l;

            switch (id) {
                case FLD_L_KIND:
                    l->kind = (OsLightKind)(int)clampd(v, 0, OS_LIGHT_KIND_COUNT - 1);
                    os_scenedesc_rehome_light(l);
                    break;
                case FLD_L_X:      l->centre.x = v; break;
                case FLD_L_Y:      l->centre.y = v; break;
                case FLD_L_Z:      l->centre.z = v; break;
                case FLD_L_RADIUS: l->radius   = v; break;
                case FLD_L_SIZEU:  l->size_u   = v; break;
                case FLD_L_SIZEV:  l->size_v   = v; break;
                case FLD_L_FLUX:   l->flux_lm  = v; break;
                /* Only the colour moves. The authored lumens stay put and the
                 * watts are recomputed from both at build time, so a 4000 lm
                 * lamp is still a 4000 lm lamp after being warmed up. */
                case FLD_L_CCT:    l->cct_k    = v; break;
                default: break;
            }
            os_scenedesc_clamp_light(l);
            return memcmp(&before, l, sizeof before) != 0;
        }
        case FLD_FOCAL: {
            double nv = clampd(v, 12.0, 400.0);
            if (nv == s->focal_mm) return false;
            s->focal_mm = nv; return true;
        }
        case FLD_FNO: {
            double nv = clampd(v, 1.0, 45.0);
            if (nv == s->fno) return false;
            s->fno = nv; return true;
        }
        case FLD_FOCUS: {
            double nv = clampd(v, 0.15, 1000.0);
            if (nv == s->focus_m) return false;
            s->focus_m = nv; return true;
        }
        case FLD_BLADES: {
            /* 0 means a circle; 1 and 2 are not shapes, so they are skipped
             * rather than clamped -- dragging up from 0 lands on 3. */
            int nv = (int)clampd(v, 0, 14);
            if (nv > 0 && nv < 3) nv = 3;
            if (nv == s->blades) return false;
            s->blades = nv; return true;
        }
        case FLD_CURVE: {
            double nv = clampd(v, 0.0, 1.0);
            if (nv == s->curvature) return false;
            s->curvature = nv; return true;
        }
        case FLD_ROT: {
            double nv = clampd(v, 0.0, 90.0);
            if (nv == s->rot_deg) return false;
            s->rot_deg = nv; return true;
        }
        case FLD_SENSOR_W: {
            double nv = clampd(v, 4.0, 80.0);
            if (nv == s->sensor_w_mm) return false;
            s->sensor_w_mm = nv; return true;
        }
        case FLD_RES: {
            int nv = (int)clampd(v, 64, 1600);
            if (nv == s->res_w) return false;
            s->res_w = nv; return true;
        }
        case FLD_COC: {
            double nv = clampd(v, 0.002, 0.2);
            if (nv == s->coc_limit_mm) return false;
            s->coc_limit_mm = nv; return true;
        }
        case FLD_EXPOSURE: {
            double nv = clampd(v, 1e-4, 1e6);
            if (nv == s->exposure) return false;
            s->exposure = nv; return true;
        }
        case FLD_SPP: {
            int nv = (int)clampd(v, 1, 256);
            if (nv == s->spp) return false;
            s->spp = nv; return true;
        }
        case FLD_DEPTH: {
            int nv = (int)clampd(v, 1, 16);
            if (nv == s->depth) return false;
            s->depth = nv; return true;
        }
        default: return false;      /* headings and derived rows */
    }
}

double os_inspect_scrub(const Field *f, double v, int dx) {
    if (f->readonly || f->heading) return v;
    if (f->is_enum) return clampd(v + (dx > 0 ? 1 : (dx < 0 ? -1 : 0)),
                                  f->lo, f->hi);

    if (f->logarithmic) {
        /* A fixed fraction per pixel, so a drag covers the same number of
         * STOPS wherever it starts. 0.6 % per pixel puts a full stop at about
         * 115 px of travel, which is a comfortable gesture. */
        double nv = (v > 1e-12 ? v : f->lo) * pow(1.006, (double)dx);
        return clampd(nv, f->lo, f->hi);
    }
    /* Integer-valued rows step one per few pixels rather than by a fraction of
     * their span, or blade count would need a 200-pixel drag to move by one. */
    if (f->hi - f->lo <= 32.0)
        return clampd(v + (double)dx * 0.08, f->lo, f->hi);
    return clampd(v + (double)dx * (f->hi - f->lo) * 0.0025, f->lo, f->hi);
}

void os_inspect_format(const Field *f, char *buf, size_t n) {
    if (f->heading) { snprintf(buf, n, "%s", ""); return; }
    if (f->is_enum) {
        int i = (int)(f->value + 0.5);
        if (i < 0) i = 0;
        if (i >= f->nnames) i = f->nnames - 1;
        snprintf(buf, n, "%s", f->names ? f->names[i] : "?");
        return;
    }
    double v = f->value;
    /* An aperture reads as f/5.6, not 5.6 f/, and a focus of 1000 m is
     * infinity as far as any lens is concerned. */
    if (f->id == FLD_FNO)                  snprintf(buf, n, "F/%.1f", v);
    else if (f->id == FLD_D_TSTOP)         snprintf(buf, n, "T/%.1f", v);
    else if ((f->id == FLD_FOCUS || f->id == FLD_D_FAR
              || f->id == FLD_D_HYPER) && (v >= 999.0 || !isfinite(v)))
                                           snprintf(buf, n, "INFINITY");
    else if (f->id == FLD_BLADES && v < 0.5)   snprintf(buf, n, "CIRCLE");
    else if (f->id == FLD_D_SAMPLES)       snprintf(buf, n, "%.0f", v);
    /* A blade count of "6.00" or a render width of "420.00 PX" reads as a
     * quantity that could be fractional. These cannot be. */
    else if (f->integral)                  snprintf(buf, n, "%.0f %s", v, f->unit);
    else if (f->id == FLD_EXPOSURE)        snprintf(buf, n, "%.4g %s", v, f->unit);
    else if (fabs(v) >= 100.0)             snprintf(buf, n, "%.1f %s", v, f->unit);
    else if (fabs(v) >= 1.0)               snprintf(buf, n, "%.2f %s", v, f->unit);
    else                                   snprintf(buf, n, "%.3f %s", v, f->unit);
}
