/* os_trace.c — a scalar path tracer. See trace.h for why it is not vendored. */
#include "opticsim/trace.h"

#include "lightsim/bsdf.h"
#include "lightsim/light.h"
#include "lightsim/core.h"

#include <math.h>

/* Power-2 MIS heuristic. Squaring sharpens the crossover between the two
 * strategies, which is what suppresses the fireflies a balance heuristic
 * leaves behind on small bright sources. */
static ls_real mis2(ls_real a, ls_real b) {
    ls_real a2 = a * a, b2 = b * b;
    ls_real s = a2 + b2;
    return s > 0.0 ? a2 / s : 0.0;
}

/* A BSDF's value at one wavelength. The vendored evaluator works in Spectrum,
 * so this reads the single bin back out -- the scene is non-dispersive, so the
 * direction it sampled is right for every wavelength and only the value
 * differs. */
static ls_real bsdf_eval1(const Bsdf *b, vec3 wo, vec3 wi, ls_real lambda_nm) {
    Spectrum f;
    ls_bsdf_eval(b, wo, wi, &f);
    return ls_spectrum_at(&f, lambda_nm);
}

/* Cosine-weighted hemisphere about +z, with its solid-angle pdf.
 *
 * The right sampler for a UNIFORM dome specifically: the estimator's cos/pdf
 * collapses to exactly pi, so a sky costs one shadow ray and contributes no
 * cosine noise of its own. Sampling the dome uniformly instead would put most
 * of the samples near the horizon, where the cosine throws them away. */
static vec3 cosine_hemisphere(ls_real u1, ls_real u2, ls_real *pdf) {
    ls_real r = sqrt(u1), phi = LS_TWO_PI * u2;
    ls_real z2 = 1.0 - u1;
    vec3 d = { r * cos(phi), r * sin(phi), sqrt(z2 > 0.0 ? z2 : 0.0) };
    *pdf = d.z / LS_PI;
    return d;
}

ls_real os_trace_radiance(const Scene *sc, const OsEnv *env, Ray ray,
                          ls_real lambda_nm, Rng *rng, int max_depth) {
    ls_real L = 0.0;
    ls_real beta = 1.0;            /* path throughput, dimensionless */
    bool prev_was_delta = true;    /* the camera ray: nothing to MIS against */
    ls_real prev_pdf = 0.0;
    /* The pdf next-event-toward-the-sky WOULD have had for the direction that
     * got us here. Kept so an escaping ray can be weighted against the other
     * strategy that could have found the sky. */
    ls_real prev_env_pdf = 0.0;

    /* A dome at zero radiance is no dome. Dropping it from the strategy count
     * here, rather than sampling it and adding zero, keeps the lamps-only
     * estimator exactly what it was before the sky existed. */
    ls_real env_le = os_env_radiance(env, ray.d, lambda_nm);
    bool have_env = (env_le > 0.0);
    int nstrat = sc->nlights + (have_env ? 1 : 0);

    for (int depth = 0; depth < max_depth; ++depth) {
        Hit hit;
        if (!ls_scene_intersect(sc, &ray, &hit)) {
            /* Escaped, so the sky is what is out there -- and it is the ONLY
             * thing an escaping ray can see, which is what makes the dome cost
             * nothing to look up. */
            if (have_env) {
                ls_real w = prev_was_delta
                    ? 1.0
                    : mis2(prev_pdf, prev_env_pdf / (ls_real)nstrat);
                L += beta * os_env_radiance(env, ray.d, lambda_nm) * w;
            }
            break;
        }

        const Material *mat = &sc->mats[hit.mat_id];

        /* ---- emission ----
         * Taken only on a BSDF-sampled (or camera) ray. NEE already accounted
         * for the light's contribution at the previous vertex, so adding it
         * again here unweighted is the classic double count. */
        if (mat->emissive && hit.light_id >= 0) {
            ls_real Le = ls_spectrum_at(&mat->le, lambda_nm);
            if (prev_was_delta) {
                L += beta * Le;
            } else {
                /* Weight it against the probability NEE would have chosen this
                 * same point, so the two strategies sum to exactly one. */
                const Light *lt = &sc->lights[hit.light_id];
                ls_real pdf_l = ls_light_pdf_w(lt, ray.o, hit.p, hit.ng)
                              / (ls_real)nstrat;
                L += beta * Le * mis2(prev_pdf, pdf_l);
            }
        }

        /* Shading uses the geometric normal flipped to face the incoming ray.
         * hit.ng is never flipped by the intersector -- that is its contract --
         * so the flip happens here, locally, and is not stored. */
        vec3 ns = hit.backface ? v3scale(hit.ng, -1.0) : hit.ng;
        vec3 wo = v3scale(ray.d, -1.0);

        Basis onb = ls_basis(ns);
        vec3 wo_l = ls_basis_to_local(onb, wo);

        /* ---- next event estimation ----
         * ONE strategy is drawn uniformly out of nstrat, and the sky is the
         * last of them. Choosing among the lamps AND the sky with a single
         * draw -- rather than always sampling the sky in addition -- is what
         * keeps the estimator unbiased when both are present. */
        if (nstrat > 0 && !ls_bsdf_is_delta(&mat->bsdf)) {
            int li = (int)(ls_rng_f(rng) * (ls_real)nstrat);
            if (li >= nstrat) li = nstrat - 1;

            if (li >= sc->nlights) {
                /* ---- the sky ---- */
                ls_real pdf_e;
                vec3 wi_l = cosine_hemisphere(ls_rng_f(rng), ls_rng_f(rng),
                                              &pdf_e);
                vec3 wi = ls_basis_to_world(onb, wi_l);
                /* HUGE_VAL because the dome is infinitely far: anything at all
                 * in the way occludes it, and there is no far end to stop
                 * short of. */
                if (pdf_e > 0.0
                    && !ls_scene_occluded(sc, hit.p, hit.ng, wi, HUGE_VAL)) {
                    ls_real f  = bsdf_eval1(&mat->bsdf, wo_l, wi_l, lambda_nm);
                    ls_real le = os_env_radiance(env, wi, lambda_nm);
                    ls_real contrib = beta * f * wi_l.z * (le / pdf_e)
                                    * (ls_real)nstrat;
                    ls_real pdf_b = ls_bsdf_pdf(&mat->bsdf, wo_l, wi_l);
                    contrib *= mis2(pdf_e / (ls_real)nstrat, pdf_b);
                    L += contrib;
                }
            } else {
                /* ---- a placed lamp ---- */
                const Light *lt = &sc->lights[li];

                LightSample s;
                if (ls_light_sample(lt, hit.p, ls_rng_f(rng), ls_rng_f(rng), &s)) {
                    ls_real cos_at_p = v3dot(ns, s.wi);
                    if (cos_at_p > 0.0
                        && !ls_scene_occluded(sc, hit.p, hit.ng, s.wi, s.dist)) {

                        vec3 wi_l = ls_basis_to_local(onb, s.wi);
                        ls_real f = bsdf_eval1(&mat->bsdf, wo_l, wi_l, lambda_nm);
                        ls_real li_over_pdf = ls_spectrum_at(&s.li_over_pdf, lambda_nm);
                        /* One strategy was chosen out of nstrat uniformly, so
                         * the estimate is scaled back up by that count. */
                        ls_real contrib = beta * f * cos_at_p * li_over_pdf
                                        * (ls_real)nstrat;

                        /* pdf_w == 0 marks a DELTA light, which no BSDF sample
                         * can ever hit -- so there is nothing to weight
                         * against and NEE takes the whole contribution. */
                        if (s.pdf_w > 0.0) {
                            ls_real pdf_b = ls_bsdf_pdf(&mat->bsdf, wo_l, wi_l);
                            contrib *= mis2(s.pdf_w / (ls_real)nstrat, pdf_b);
                        }
                        L += contrib;
                    }
                }
            }
        }

        /* ---- continue along a BSDF-sampled direction ---- */
        vec3 wi_l;
        Spectrum f_s;
        ls_real pdf;
        if (!ls_bsdf_sample(&mat->bsdf, wo_l, ls_rng_f(rng), ls_rng_f(rng),
                            &wi_l, &f_s, &pdf))
            break;
        if (!(pdf > 0.0)) break;

        ls_real f = ls_spectrum_at(&f_s, lambda_nm);
        ls_real cos_i = fabs(wi_l.z);
        beta *= f * cos_i / pdf;
        if (!(beta > 0.0)) break;

        prev_was_delta = ls_bsdf_is_delta(&mat->bsdf);
        prev_pdf = pdf;
        /* The same direction, priced by the SKY's sampler -- what NEE toward
         * the dome would have called this. A delta BSDF has no such price and
         * prev_was_delta suppresses its use. */
        prev_env_pdf = cos_i / LS_PI;

        vec3 wi = ls_basis_to_world(onb, wi_l);
        ray.o = ls_offset_origin(hit.p, hit.ng, wi);
        ray.d = wi;
        ray.tmin = 0.0;
        ray.tmax = HUGE_VAL;

        /* Russian roulette from depth 3, on the scalar throughput. Starting
         * earlier saves little and adds variance to the bounces that carry most
         * of the image. */
        if (depth >= 3) {
            ls_real q = beta < 0.95 ? beta : 0.95;
            if (ls_rng_f(rng) > q) break;
            beta /= q;
        }
    }
    return L;
}
