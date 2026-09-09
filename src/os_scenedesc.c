/* os_scenedesc.c — the editable scene, and the one place a flat Scene is made
 * from it. See scenedesc.h for the id invariant and why the limits exist. */
#include "opticsim/scenedesc.h"

#include "lightsim/color.h"
#include "lightsim/core.h"
#include "lightsim/units.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- limits ---- */

static ls_real clampd(ls_real v, ls_real lo, ls_real hi) {
    /* Non-finite is mapped to lo rather than passed through. A NaN fails both
     * comparisons in the obvious three-way clamp, so it survives -- and a NaN
     * position removes the object from every ray query without a word. */
    if (!isfinite(v)) return lo;
    return v < lo ? lo : (v > hi ? hi : v);
}

void os_scenedesc_clamp_object(OsObject *o) {
    o->centre.x = clampd(o->centre.x, -OS_POS_LIMIT_M, OS_POS_LIMIT_M);
    o->centre.y = clampd(o->centre.y, -OS_POS_LIMIT_M, OS_POS_LIMIT_M);
    o->centre.z = clampd(o->centre.z, -OS_POS_LIMIT_M, OS_POS_LIMIT_M);
    o->radius   = clampd(o->radius, OS_SIZE_MIN_M, OS_SIZE_MAX_M);
    for (int i = 0; i < 3; ++i) o->rgb[i] = clampd(o->rgb[i], 0.0, 1.0);

    /* A plane needs a direction, and normalising a zero vector gives NaN. */
    if (v3len(o->normal) < 1e-9) o->normal = v3(0.0, 0.0, 1.0);
    else                         o->normal = v3norm(o->normal);
}

void os_scenedesc_clamp_light(OsLight *l) {
    l->centre.x = clampd(l->centre.x, -OS_POS_LIMIT_M, OS_POS_LIMIT_M);
    l->centre.y = clampd(l->centre.y, -OS_POS_LIMIT_M, OS_POS_LIMIT_M);
    l->centre.z = clampd(l->centre.z, -OS_POS_LIMIT_M, OS_POS_LIMIT_M);

    /* THE clamps that keep ls_light_finalize from aborting. A zero extent
     * makes radiance = flux/(area*pi) infinite; a colour temperature below the
     * visible underflows every bin so the spectral shape cannot be normalised.
     * Both trip an assert inside the build, nowhere near the control that was
     * moved. */
    l->radius  = clampd(l->radius, OS_SIZE_MIN_M, OS_SIZE_MAX_M);
    l->size_u  = clampd(l->size_u, OS_SIZE_MIN_M, OS_SIZE_MAX_M);
    l->size_v  = clampd(l->size_v, OS_SIZE_MIN_M, OS_SIZE_MAX_M);
    l->cct_k   = clampd(l->cct_k,  OS_CCT_MIN_K,  OS_CCT_MAX_K);
    l->flux_lm = clampd(l->flux_lm, 0.0, OS_FLUX_MAX_LM);
}

void os_scenedesc_clamp_ambient(OsSceneDesc *d) {
    d->ambient_lux   = clampd(d->ambient_lux, 0.0, OS_AMBIENT_MAX_LX);
    /* Same reason as a lamp's: below about 1200 K a blackbody underflows every
     * visible bin, and the shape cannot be normalised. */
    d->ambient_cct_k = clampd(d->ambient_cct_k, OS_CCT_MIN_K, OS_CCT_MAX_K);
    if (d->light_mode < 0 || d->light_mode >= OS_LIGHT_MODE_COUNT)
        d->light_mode = OS_LIGHT_LAMPS;
}

void os_scenedesc_rehome_object(OsObject *o) {
    /* Unconditional, not "only if unset". */
    switch (o->kind) {
        case OS_OBJ_SPHERE:
            if (!(o->radius >= OS_SIZE_MIN_M && o->radius <= OS_SIZE_MAX_M))
                o->radius = 0.06;
            break;
        case OS_OBJ_PLANE:
            if (v3len(o->normal) < 1e-9) o->normal = v3(0.0, 0.0, 1.0);
            break;
        default: break;
    }
    os_scenedesc_clamp_object(o);
}

void os_scenedesc_rehome_light(OsLight *l) {
    switch (l->kind) {
        case OS_LIGHT_SPHERE:
            if (!(l->radius >= OS_SIZE_MIN_M && l->radius <= OS_SIZE_MAX_M))
                l->radius = 0.05;
            break;
        case OS_LIGHT_RECT:
            if (!(l->size_u >= OS_SIZE_MIN_M && l->size_u <= OS_SIZE_MAX_M))
                l->size_u = 0.5;
            if (!(l->size_v >= OS_SIZE_MIN_M && l->size_v <= OS_SIZE_MAX_M))
                l->size_v = 0.5;
            break;
        default: break;
    }
    os_scenedesc_clamp_light(l);
}

/* ---- authoring ---- */

int os_scenedesc_add_object(OsSceneDesc *d, OsObjKind k) {
    if (d->nobj >= OS_MAX_OBJECTS) return -1;
    int id = d->nobj++;
    OsObject *o = &d->obj[id];
    memset(o, 0, sizeof *o);
    o->alive  = true;
    o->kind   = k;
    o->centre = v3(0.0, 0.0, -2.0);
    o->radius = 0.06;
    o->normal = v3(0.0, 0.0, 1.0);
    o->rgb[0] = o->rgb[1] = o->rgb[2] = 0.75;
    snprintf(o->name, sizeof o->name, "OBJ %d", id + 1);
    os_scenedesc_rehome_object(o);
    return id;
}

int os_scenedesc_add_light(OsSceneDesc *d, OsLightKind k) {
    if (d->nlit >= OS_MAX_LIGHTS) return -1;
    int id = d->nlit++;
    OsLight *l = &d->lit[id];
    memset(l, 0, sizeof *l);
    l->alive   = true;
    l->kind    = k;
    l->centre  = v3(1.2, 1.4, -1.4);
    l->radius  = 0.05;
    l->size_u  = 0.5;
    l->size_v  = 0.5;
    l->flux_lm = 4000.0;
    l->cct_k   = 5500.0;
    snprintf(l->name, sizeof l->name, "LAMP %d", id + 1);
    os_scenedesc_rehome_light(l);
    return id;
}

bool os_scenedesc_delete_object(OsSceneDesc *d, int id) {
    if (id < 0 || id >= d->nobj || !d->obj[id].alive) return false;
    d->obj[id].alive = false;     /* tombstone; nobj deliberately unchanged */
    return true;
}

bool os_scenedesc_delete_light(OsSceneDesc *d, int id) {
    if (id < 0 || id >= d->nlit || !d->lit[id].alive) return false;
    d->lit[id].alive = false;
    return true;
}

int os_scenedesc_count_objects(const OsSceneDesc *d) {
    int n = 0;
    for (int i = 0; i < d->nobj; ++i) if (d->obj[i].alive) n++;
    return n;
}

int os_scenedesc_count_lights(const OsSceneDesc *d) {
    int n = 0;
    for (int i = 0; i < d->nlit; ++i) if (d->lit[i].alive) n++;
    return n;
}

int os_scenedesc_next_object(const OsSceneDesc *d, int from) {
    for (int i = from < 0 ? 0 : from; i < d->nobj; ++i)
        if (d->obj[i].alive) return i;
    return -1;
}

int os_scenedesc_next_light(const OsSceneDesc *d, int from) {
    for (int i = from < 0 ? 0 : from; i < d->nlit; ++i)
        if (d->lit[i].alive) return i;
    return -1;
}

ls_real os_scenedesc_depth(const OsSceneDesc *d, int id) {
    if (id < 0 || id >= d->nobj || !d->obj[id].alive) return -1.0;
    /* The camera looks down -z from the origin, so depth is just -z. Derived
     * on demand rather than recorded, so it cannot drift from the object. */
    return -d->obj[id].centre.z;
}

/* ---- building the flat Scene ---- */

/* Room for one prim and one material per object, plus one of each per light
 * for its emissive face. */
#define MAX_PRIMS (OS_MAX_OBJECTS + OS_MAX_LIGHTS)
#define MAX_MATS  (OS_MAX_OBJECTS + OS_MAX_LIGHTS)

bool os_scenedesc_build(const OsSceneDesc *d, OsStage *out) {
    memset(out, 0, sizeof *out);
    out->prims  = calloc(MAX_PRIMS,     sizeof *out->prims);
    out->mats   = calloc(MAX_MATS,      sizeof *out->mats);
    out->lights = calloc(OS_MAX_LIGHTS, sizeof *out->lights);
    if (!out->prims || !out->mats || !out->lights) { os_stage_free(out); return false; }

    out->cam_eye    = d->cam_eye;
    out->cam_target = d->cam_target;

    int np = 0, nm = 0, nl = 0;

    /* ---- subjects ---- */
    for (int i = 0; i < d->nobj; ++i) {
        if (!d->obj[i].alive) continue;
        OsObject o = d->obj[i];
        os_scenedesc_clamp_object(&o);

        Material m;
        memset(&m, 0, sizeof m);
        m.bsdf.kind = LS_BSDF_LAMBERT;
        /* Authored as a colour and uplifted to a smooth spectrum. The uplift
         * is not invertible, so rgb[] stays the source of truth. */
        m.bsdf.rho = ls_spectrum_from_rgb_reflectance(
            (RGB){ o.rgb[0], o.rgb[1], o.rgb[2] });
        m.rgb[0] = o.rgb[0]; m.rgb[1] = o.rgb[1]; m.rgb[2] = o.rgb[2];
        m.from_rgb = true;
        int mid = nm;
        out->mats[nm++] = m;

        Prim p;
        memset(&p, 0, sizeof p);
        p.mat_id = mid;
        p.light_id = -1;
        p.mesh_id = -1;
        if (o.kind == OS_OBJ_PLANE) {
            p.kind = LS_PRIM_PLANE;
            p.c = o.centre;
            p.n = o.normal;
        } else {
            p.kind = LS_PRIM_SPHERE;
            p.c = o.centre;
            p.r = o.radius;
        }

        /* The ground truth the stage exists to provide, recorded against the
         * prim that was actually built. */
        if (out->nmarkers < OS_STAGE_MAX_MARKERS) {
            snprintf(out->marker[out->nmarkers].label,
                     sizeof out->marker[0].label, "%s", o.name);
            out->marker[out->nmarkers].prim = np;
            out->marker[out->nmarkers].depth_m = -o.centre.z;
            out->nmarkers++;
        }
        out->prims[np++] = p;
    }

    /* ---- lights, each with the emissive face that IS it ----
     *
     * THE only place the pairing is made. A Light and the prim carrying its
     * emissive material are created together here and exist only for the life
     * of this build, so they cannot drift apart -- which in the sibling repo
     * takes two renumberings and a dedicated sync function to guarantee. */
    for (int i = 0; i < d->nlit && d->light_mode == OS_LIGHT_LAMPS; ++i) {
        if (!d->lit[i].alive) continue;
        if (nl >= OS_MAX_LIGHTS || np >= MAX_PRIMS || nm >= MAX_MATS) break;

        OsLight L = d->lit[i];
        os_scenedesc_clamp_light(&L);

        Spectrum spd = ls_spectrum_blackbody(L.cct_k);
        /* Lumens in, watts stored. units.h owns this conversion and says so:
         * "callers convert here and hand watts to the light constructors, so
         * no lumen is ever stored". */
        ls_real watts = ls_watts_from_lumens(L.flux_lm, &spd);

        int id = nl;
        Light lt;
        if (L.kind == OS_LIGHT_RECT) {
            lt = ls_light_rect(L.centre,
                               v3(L.size_u * 0.5, 0.0, 0.0),
                               v3(0.0, 0.0, L.size_v * 0.5),
                               watts, spd);
        } else {
            lt = ls_light_sphere(L.centre, L.radius, watts, spd);
        }
        /* Kept so the authored number survives into the built scene; the
         * vendored Light carries these fields for exactly this. */
        lt.flux_in_lumens = true;
        lt.flux_authored  = L.flux_lm;
        ls_light_finalize(&lt, id);
        out->lights[nl++] = lt;

        Material em;
        memset(&em, 0, sizeof em);
        em.bsdf.kind = LS_BSDF_LAMBERT;
        em.bsdf.rho  = ls_spectrum_zero();
        /* The face carries the light's OWN radiance, so what the camera sees
         * and what next-event estimation samples agree by construction. */
        em.le        = ls_spectrum_scale(lt.s_hat, lt.radiance);
        em.emissive  = true;
        int mid = nm;
        out->mats[nm++] = em;

        Prim p;
        memset(&p, 0, sizeof p);
        p.mat_id = mid;
        p.light_id = id;
        p.mesh_id = -1;
        if (L.kind == OS_LIGHT_RECT) {
            p.kind = LS_PRIM_QUAD;
            p.c  = L.centre;
            p.ex = v3(L.size_u * 0.5, 0.0, 0.0);
            p.ey = v3(0.0, 0.0, L.size_v * 0.5);
            p.n  = v3norm(v3cross(p.ex, p.ey));
        } else {
            p.kind = LS_PRIM_SPHERE;
            p.c = L.centre;
            p.r = L.radius;
        }
        out->prims[np++] = p;
    }

    /* ---- the dome ----
     *
     * Built here rather than in the tracer for the same reason lamps are: this
     * is the one place a photometric number becomes a radiometric one. The
     * lux the panel holds is the illuminance on a surface facing the sky, and
     * a uniform dome of radiance L delivers exactly pi*L there -- so the
     * radiance is E/pi, and ls_watts_from_lumens does the photometric half of
     * the conversion, per unit area, exactly as it does for a lamp's flux.
     *
     * The dome carries NO GEOMETRY: no prim, no entry in scene.lights. It
     * cannot be hit, so the tracer samples it as a separate strategy. See
     * env.h. */
    if (d->light_mode == OS_LIGHT_AMBIENT) {
        OsSceneDesc amb = *d;
        os_scenedesc_clamp_ambient(&amb);
        Spectrum shape = ls_spectrum_normalize_to(
            ls_spectrum_blackbody(amb.ambient_cct_k), 1.0);
        /* Lux in, W/m^2 out: the same algebra as lumens to watts, one factor
         * of area down on both sides. units.h owns the conversion. */
        ls_real e_perp = ls_watts_from_lumens(amb.ambient_lux, &shape);
        out->env.on = true;
        out->env.le = ls_spectrum_scale(shape, e_perp / LS_PI);
    }

    out->scene.prims  = out->prims;   out->scene.nprims  = np;
    out->scene.mats   = out->mats;    out->scene.nmats   = nm;
    out->scene.lights = out->lights;  out->scene.nlights = nl;
    out->scene.meshes = NULL;         out->scene.nmeshes = 0;
    return true;
}
