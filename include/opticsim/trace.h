/* trace.h — light transport at ONE wavelength.
 *
 * THE INVARIANT THIS MODULE OWNS
 *   Radiance along a path is a SCALAR. No Spectrum crosses a path vertex.
 *
 *   That is why lightsim's integrator.c is deliberately not vendored: it
 *   carries all 95 bins down every path, and hero-wavelength sampling uses
 *   exactly one of them. Vendoring it would mean computing 95 bins of shading
 *   to throw 94 away at the film, then forking it later to fix that. Writing
 *   the scalar version once is less code and less waste.
 *
 *   Every spectral quantity the scene owns -- reflectance, emission, a light's
 *   spectral shape -- is read at the path's wavelength with ls_spectrum_at().
 *   This is legitimate precisely because scene materials are non-dispersive:
 *   wavelength changes the VALUE of a BSDF here, never a sampled direction, so
 *   one geometric path is correct for the one wavelength it carries.
 *
 * THE ESTIMATOR
 *   Next-event estimation at every vertex, plus the BSDF-sampled continuation,
 *   combined with the power-2 MIS heuristic -- so a small bright light and a
 *   large dim one both converge, and neither is counted twice. Emission is
 *   taken on a BSDF-sampled ray only, never on a NEE ray, which is what keeps
 *   the two from double counting.
 */
#ifndef OPTICSIM_TRACE_H
#define OPTICSIM_TRACE_H

#include "opticsim/env.h"
#include "lightsim/scene.h"
#include "lightsim/rng.h"

/* Radiance reaching the ray's origin from its direction, at `lambda_nm`,
 * in W/(m^2 sr nm).
 *
 * `env` may be NULL, which means empty space: a ray that escapes the scene
 * carries nothing. When it is on it is ONE MORE SAMPLING STRATEGY alongside
 * the placed lights -- it joins the uniform light choice, so every 1/nlights
 * in the estimator becomes 1/(nlights+1). Adding it as an unweighted extra
 * term instead would double count it against the BSDF-sampled ray that
 * escapes, and the scene would come out brighter the more bounces it was
 * given. */
ls_real os_trace_radiance(const Scene *sc, const OsEnv *env, Ray ray,
                          ls_real lambda_nm, Rng *rng, int max_depth);

#endif /* OPTICSIM_TRACE_H */
