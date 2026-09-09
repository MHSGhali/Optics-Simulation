/* os_glass.c — Sellmeier evaluation, Abbe number, and the model-glass solve.
 *
 * See glass.h for why dispersion is confined to this layer. The data lives in
 * os_glass_data.c.
 */
#include "opticsim/glass.h"

#include <math.h>
#include <stdio.h>

ls_real os_glass_n(const OsGlass *g, ls_real lambda_nm) {
    /* Nanometres in, micrometres for the maths. THE one place this conversion
     * happens; C_i are published in um^2 and a stray factor of 1000 inside a
     * square root yields a plausible-looking wrong index rather than a NaN. */
    ls_real l = lambda_nm * 1e-3;
    ls_real l2 = l * l;

    ls_real n2m1 = 0.0;
    for (int i = 0; i < 3; ++i) {
        ls_real d = l2 - g->C[i];
        /* A resonance sits exactly at C_i, where the term blows up. No visible
         * wavelength lands on a real glass's resonance (they are in the UV and
         * the far IR), so this guard is for a corrupt table, not for physics --
         * and skipping the term is the quiet behaviour rather than an Inf that
         * propagates into every ray. */
        if (fabs(d) < 1e-12) continue;
        n2m1 += g->B[i] * l2 / d;
    }

    /* Air has all B zero, so n2m1 is exactly 0 and this returns exactly 1.0 --
     * no sqrt rounding. That exactness is asserted by the tests, because the
     * lens tracer compares indices to decide whether a surface refracts at all. */
    ls_real n2 = 1.0 + n2m1;
    return n2 > 0.0 ? sqrt(n2) : 1.0;
}

ls_real os_glass_dispersion(const OsGlass *g) {
    return os_glass_n(g, OS_LINE_F) - os_glass_n(g, OS_LINE_C);
}

ls_real os_glass_abbe(const OsGlass *g) {
    ls_real dn = os_glass_dispersion(g);
    /* Air, and any hypothetical dispersionless medium, divide by zero here.
     * Returning 0 rather than HUGE_VAL keeps it printable and keeps a caller
     * that sums Abbe numbers from producing a NaN. Callers that care about the
     * difference should ask whether the glass is air. */
    if (fabs(dn) < 1e-15) return 0.0;
    return (os_glass_n(g, OS_LINE_D) - 1.0) / dn;
}

void os_glass_constant(ls_real n, OsGlass *out) {
    /* C = 0 makes lambda^2/(lambda^2 - C) identically 1, so this single term
     * contributes n^2 - 1 at every wavelength and os_glass_n returns exactly n.
     * No branch in the evaluator, no dispersion, no rounding. */
    out->B[0] = n * n - 1.0; out->B[1] = 0.0; out->B[2] = 0.0;
    out->C[0] = 0.0;         out->C[1] = 0.0; out->C[2] = 0.0;
    out->nd_published = 0.0;
    out->vd_published = 0.0;
    out->name = "ideal";
}

/* ---- model glasses ------------------------------------------------------
 *
 * Two-term Sellmeier with the resonances pinned, so only B_1 and B_2 are free:
 *
 *     n^2(l) - 1 = B1 l^2/(l^2 - C1) + B2 l^2/(l^2 - C2),   C1 = 0.01, C2 = 100
 *
 * Constraint (a), n(l_d) = nd, is LINEAR in (B1, B2), so B2 is eliminated:
 *
 *     B2 = (nd^2 - 1 - B1 g1(l_d)) / g2(l_d)
 *
 * Constraint (b), n(l_F) - n(l_C) = (nd - 1)/vd, is not linear, but with B2
 * eliminated it is a smooth scalar function of B1 alone -- so a secant
 * iteration closes it in a handful of steps with no derivative to get wrong. */

#define MODEL_C1 0.01     /* um^2, a UV resonance below the visible band */
#define MODEL_C2 100.0    /* um^2, an IR resonance above it              */

static ls_real gterm(ls_real lambda_nm, ls_real C) {
    ls_real l2 = lambda_nm * 1e-3 * (lambda_nm * 1e-3);
    return l2 / (l2 - C);
}

/* Residual of constraint (b) for a trial B1, with B2 pinned by constraint (a). */
static ls_real model_residual(ls_real B1, ls_real nd, ls_real target_dn,
                              OsGlass *g) {
    ls_real B2 = ((nd * nd - 1.0) - B1 * gterm(OS_LINE_D, MODEL_C1))
                 / gterm(OS_LINE_D, MODEL_C2);
    g->B[0] = B1;  g->B[1] = B2;  g->B[2] = 0.0;
    g->C[0] = MODEL_C1; g->C[1] = MODEL_C2; g->C[2] = 0.0;
    return os_glass_dispersion(g) - target_dn;
}

bool os_glass_model(ls_real nd, ls_real vd, OsGlass *out) {
    if (!(nd > 1.0) || !(vd > 0.0)) return false;

    OsGlass g;
    g.nd_published = 0.0;   /* a model glass has nothing published to check */
    g.vd_published = 0.0;
    g.name = "model";

    ls_real target_dn = (nd - 1.0) / vd;

    /* Secant needs two seeds that bracket loosely. B1 near n^2-1 puts almost
     * all the index on the UV term, which is where a real crown sits; the
     * second seed is deliberately far away so the first step is informative. */
    ls_real x0 = nd * nd - 1.0;
    ls_real x1 = x0 * 0.5;
    ls_real f0 = model_residual(x0, nd, target_dn, &g);
    ls_real f1 = model_residual(x1, nd, target_dn, &g);

    for (int it = 0; it < 100; ++it) {
        ls_real denom = f1 - f0;
        if (fabs(denom) < 1e-18) break;          /* flat: no secant step exists */
        ls_real x2 = x1 - f1 * (x1 - x0) / denom;
        ls_real f2 = model_residual(x2, nd, target_dn, &g);
        x0 = x1; f0 = f1;
        x1 = x2; f1 = f2;
        if (fabs(f2) < 1e-15) break;
    }

    /* Verify rather than trust the loop. A glass that converged to the wrong
     * root has a plausible index and a wrong dispersion, and downstream it
     * looks like a lens design error instead of a data error -- so the caller
     * gets told here, at the only point where the distinction is still clear. */
    (void)model_residual(x1, nd, target_dn, &g);
    if (!(fabs(os_glass_n(&g, OS_LINE_D) - nd) < 1e-9)) return false;
    if (!(fabs(os_glass_dispersion(&g) - target_dn) < 1e-12)) return false;

    *out = g;
    return true;
}

/* ---- data integrity ---------------------------------------------------- */

bool os_glass_self_check(char *why, size_t n) {
    for (int id = 0; id < OS_GLASS_COUNT; ++id) {
        const OsGlass *g = os_glass((OsGlassId)id);
        if (g->nd_published <= 0.0) continue;    /* air, or a model glass */

        ls_real nd = os_glass_n(g, OS_LINE_D);
        ls_real vd = os_glass_abbe(g);

        /* Catalogues print n_d to five decimals, so 1e-4 is about one unit in
         * the last published place -- tight enough to catch a mistyped digit,
         * loose enough not to fire on the rounding in the published value. */
        if (fabs(nd - g->nd_published) > 1e-4) {
            snprintf(why, n, "%s: n_d computed %.6f, published %.5f",
                     g->name, nd, g->nd_published);
            return false;
        }
        /* V_d is a ratio of small differences, so it amplifies coefficient
         * error; 0.05 is roughly one unit in its last published place. */
        if (fabs(vd - g->vd_published) > 0.05) {
            snprintf(why, n, "%s: V_d computed %.3f, published %.2f",
                     g->name, vd, g->vd_published);
            return false;
        }
    }
    if (n > 0) why[0] = '\0';
    return true;
}
