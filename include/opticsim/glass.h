/* glass.h — refractive index as a function of wavelength.
 *
 * THE INVARIANT THIS MODULE OWNS
 *   The refractive index is a function of (glass, wavelength) and of nothing
 *   else, and every `n` in this program comes from here. There is no default
 *   index, no "about 1.5", and no per-surface override.
 *
 *   The failure mode that buys: a hard-coded 1.5 somewhere in the tracer
 *   deletes chromatic aberration completely while leaving every image
 *   perfectly plausible. There is no visual tell. A lens with no dispersion
 *   just looks like a good lens. Confining n to one function means the only
 *   way to lose colour is to delete this file.
 *
 * DISPERSION LIVES HERE AND ONLY HERE
 *   lightsim/spectrum.h explains at length why the vendored transport core may
 *   carry all 95 bins down one path: every SCENE material is non-dispersive.
 *   This module is the exception that forced hero-wavelength sampling, and it
 *   is deliberately fenced off -- it is reached from the sequential lens tracer
 *   and never from a Bsdf. check-lens-purity in the Makefile enforces that
 *   fence by refusing to let os_glass_n() be called outside the lens layer.
 *
 * THE MODEL
 *   Three-term Sellmeier, the form every glass catalogue publishes:
 *
 *       n^2(lambda) - 1 = sum_i  B_i lambda^2 / (lambda^2 - C_i)
 *
 *   with lambda in MICROMETRES and C_i in um^2. Nanometres are the unit
 *   everywhere else in this program, so os_glass_n() takes nanometres and
 *   converts internally: that division by 1000 happens in exactly one place,
 *   for the same reason spectrum.c converts Planck's law per-metre to per-nm in
 *   exactly one place. A stray factor of 1000 inside a square root does not
 *   produce an obviously broken number, it produces a lens that is subtly the
 *   wrong shape.
 *
 * WHY A SELF-CHECK EXISTS
 *   These coefficients are transcribed from catalogues. A mistyped digit in
 *   B_2 or C_3 shifts the index in the fourth decimal place -- invisible by
 *   eye, and it changes the colour correction of any lens built on it. So each
 *   catalogue entry carries its PUBLISHED n_d and V_d beside its coefficients,
 *   and os_glass_self_check() asserts the Sellmeier evaluation reproduces both.
 *   The data checks itself; nobody has to trust the typing.
 */
#ifndef OPTICSIM_GLASS_H
#define OPTICSIM_GLASS_H

#include "lightsim/core.h"

/* The three Fraunhofer lines that define n_d and the Abbe number. Blue F and
 * red C bracket the visible band; yellow d is where "the" index is quoted. */
#define OS_LINE_F 486.1327   /* H  F, blue   */
#define OS_LINE_D 587.5618   /* He d, yellow */
#define OS_LINE_C 656.2725   /* H  C, red    */

/* A glass is a VALUE, not a handle into a registry.
 *
 * Passing it by value costs 64 bytes per surface and buys the absence of
 * global mutable state: a model glass synthesised at runtime (see
 * os_glass_model) is the same kind of thing as a catalogue entry, needs no
 * allocation, no id assignment and no lock, and a prescription that holds its
 * glasses by value cannot dangle. */
typedef struct {
    double      B[3], C[3];   /* Sellmeier coefficients; C in um^2 */
    double      nd_published; /* 0 for a model glass: nothing to check against */
    double      vd_published;
    const char *name;
} OsGlass;

/* ---- the catalogue ---- */

typedef enum {
    OS_GLASS_AIR = 0,
    OS_GLASS_N_BK7, OS_GLASS_N_SK16, OS_GLASS_N_LAK9, OS_GLASS_N_BAF10,
    OS_GLASS_N_LAF2, OS_GLASS_F2, OS_GLASS_SF2, OS_GLASS_N_SF5, OS_GLASS_SF11,
    OS_GLASS_COUNT
} OsGlassId;

const OsGlass *os_glass(OsGlassId id);

/* ---- evaluation ---- */

/* Refractive index at `lambda_nm`. Air returns exactly 1.0 at every
 * wavelength -- not Edlen's formula for real air, which differs in the fourth
 * decimal. That is deliberate: a prescription's air gaps are design values
 * measured against vacuum-referenced indices, so introducing real air here
 * would shift every lens's focus by microns and make the paraxial self-checks
 * fail for a reason that looks like a coding error. */
ls_real os_glass_n(const OsGlass *g, ls_real lambda_nm);

/* Abbe number V_d = (n_d - 1) / (n_F - n_C), computed from the coefficients
 * rather than read from the published field, so it can be compared against it.
 * Returns 0 for air, which has no dispersion to divide by. */
ls_real os_glass_abbe(const OsGlass *g);

/* Mean dispersion n_F - n_C. */
ls_real os_glass_dispersion(const OsGlass *g);

/* ---- model glasses ---- */

/* Synthesise a glass reproducing (nd, vd) EXACTLY.
 *
 * This exists because published lens prescriptions almost never name their
 * glasses -- they print two numbers per element, n_d and V_d, and leave the
 * catalogue match to the reader. Guessing "nearest catalogue entry" gets n_d
 * right and V_d wrong, which is precisely backwards: V_d is what decides the
 * lens's colour correction, and colour correction is what we are here to show.
 *
 * Two-term Sellmeier with C_1 and C_2 pinned at 0.01 and 100 um^2 -- one
 * resonance below the visible band and one above it, which is the physical
 * shape of a real glass -- leaving B_1 and B_2 to be solved from the two
 * constraints. The first constraint is linear in B, so B_2 is eliminated and a
 * secant iteration on B_1 closes the second.
 *
 * Returns false if the solve did not converge, which happens for physically
 * impossible (nd, vd) pairs. Refuse to build a lens on a glass that failed:
 * an unconverged glass has a plausible index and a wrong dispersion, and it
 * will look like a lens design error rather than a data error. */
bool os_glass_model(ls_real nd, ls_real vd, OsGlass *out);

/* A dispersionless glass of index `n`, exactly, at every wavelength.
 *
 * Not physical -- no real material has zero dispersion -- and that is the
 * point: it is how the IDEAL thin lens is expressed, so the exposure tests
 * have an optic that contributes no aberration and no colour of its own.
 *
 * The trick is that a Sellmeier term with C = 0 reduces to a constant:
 * lambda^2/(lambda^2 - 0) is 1 for every lambda, so B_1 = n^2 - 1 gives
 * n^2(lambda) = n^2 identically, with no special case anywhere in the
 * evaluator. */
void os_glass_constant(ls_real n, OsGlass *out);

/* ---- data integrity ---- */

/* Assert that every catalogue entry's Sellmeier coefficients reproduce its own
 * published n_d and V_d. Writes the first failure into `why` and returns
 * false. Called by the test suite and by `opticsim glass`. */
bool os_glass_self_check(char *why, size_t n);

#endif /* OPTICSIM_GLASS_H */
