/* os_prescription_data.c — the shipped lens designs.
 *
 * See prescription.h for the sign convention and for why the two 100 mm
 * designs are derived rather than transcribed.
 */
#include "opticsim/prescription.h"

#include <math.h>
#include <string.h>

bool os_glassref_resolve(OsGlassRef ref, OsGlass *out) {
    switch (ref.kind) {
        case OS_GREF_MODEL:
            return os_glass_model(ref.nd, ref.vd, out);
        case OS_GREF_CONSTANT:
            if (!(ref.n >= 1.0)) return false;
            os_glass_constant(ref.n, out);
            return true;
        case OS_GREF_CATALOGUE:
        default:
            if (ref.id < 0 || ref.id >= OS_GLASS_COUNT) return false;
            *out = *os_glass(ref.id);
            return true;
    }
}

/* ---- the derived test articles ------------------------------------------
 *
 * Both are built from the thin-lens design equations at fetch time, so their
 * radii carry full double precision rather than the four or five digits a
 * hand-derivation would print. That matters more than it looks: the flint's
 * rear radius comes out as the reciprocal of a DIFFERENCE of two similar
 * numbers (1/R2 and phi2/(n2-1) are both about 0.021 and differ in the third
 * significant figure), so rounding the inputs to five places moves R3 by
 * several millimetres out of eight hundred.
 *
 * SINGLET: equiconvex, so with R2 = -R1 the thin-lens maker's equation
 *
 *     phi = (n - 1)(1/R1 - 1/R2) = (n - 1)(2/R1)
 *
 * gives R1 = 2(n_d - 1)f. For N-BK7 at f = 100 that is 103.36 mm.
 *
 * ACHROMAT: the Fraunhofer condition. Two thin elements in contact correct
 * colour when their powers are split by their Abbe numbers,
 *
 *     phi1 =  phi V1/(V1 - V2)        phi2 = -phi V2/(V1 - V2)
 *
 * because then phi1/V1 + phi2/V2 = 0, which is exactly the statement that the
 * F and C focal lengths coincide. The crown is made equiconvex (a free choice
 * -- the condition fixes the powers, not the shapes) and the flint's front
 * radius is then forced to match the crown's rear so the two can be cemented:
 *
 *     R1 = 2(n1 - 1)/phi1,   R2 = -R1,   1/R3 = 1/R2 - phi2/(n2 - 1)
 */

static void build_singlet(OsPrescription *p) {
    OsGlass bk7 = *os_glass(OS_GLASS_N_BK7);
    const ls_real f = 100.0;
    ls_real R = 2.0 * (os_glass_n(&bk7, OS_LINE_D) - 1.0) * f;

    p->name   = "singlet100";
    p->source = "derived: equiconvex N-BK7, thin-lens f = 100 mm at the d line";
    p->nsurf  = 2;
    p->stop_index = 0;                 /* the front surface is its own stop */
    p->design_efl_mm   = f;
    p->design_fno      = 10.0;         /* semi-aperture 5 mm at f = 100 mm  */
    p->image_circle_mm = 20.0;

    p->surf[0] = (OsSurface){ .radius_mm = +R, .thickness_mm = 4.0,
                              .semi_ap_mm = 10.0, .glass = OS_GREF(OS_GLASS_N_BK7),
                              .is_stop = true };
    p->surf[1] = (OsSurface){ .radius_mm = -R, .thickness_mm = 0.0,
                              .semi_ap_mm = 10.0, .glass = OS_GREF(OS_GLASS_AIR),
                              .is_stop = false };
}

static void build_achromat(OsPrescription *p) {
    OsGlass crown = *os_glass(OS_GLASS_N_BK7);
    OsGlass flint = *os_glass(OS_GLASS_F2);

    const ls_real f = 100.0;
    ls_real phi = 1.0 / f;

    ls_real v1 = os_glass_abbe(&crown), v2 = os_glass_abbe(&flint);
    ls_real n1 = os_glass_n(&crown, OS_LINE_D), n2 = os_glass_n(&flint, OS_LINE_D);

    ls_real phi1 =  phi * v1 / (v1 - v2);
    ls_real phi2 = -phi * v2 / (v1 - v2);

    ls_real R1 = 2.0 * (n1 - 1.0) / phi1;      /* crown, equiconvex */
    ls_real R2 = -R1;                          /* cemented interface */
    ls_real R3 = 1.0 / (1.0 / R2 - phi2 / (n2 - 1.0));

    p->name   = "achromat100";
    p->source = "derived: Fraunhofer N-BK7 + F2 cemented doublet, f = 100 mm";
    p->nsurf  = 3;
    p->stop_index = 0;
    p->design_efl_mm   = f;
    p->design_fno      = 5.0;                  /* semi-aperture 10 mm at f = 100 */
    p->image_circle_mm = 20.0;

    p->surf[0] = (OsSurface){ .radius_mm = R1, .thickness_mm = 4.0,
                              .semi_ap_mm = 10.0, .glass = OS_GREF(OS_GLASS_N_BK7),
                              .is_stop = true };
    p->surf[1] = (OsSurface){ .radius_mm = R2, .thickness_mm = 2.5,
                              .semi_ap_mm = 10.0, .glass = OS_GREF(OS_GLASS_F2),
                              .is_stop = false };
    p->surf[2] = (OsSurface){ .radius_mm = R3, .thickness_mm = 0.0,
                              .semi_ap_mm = 10.0, .glass = OS_GREF(OS_GLASS_AIR),
                              .is_stop = false };
}

/* An ideal thin lens, expressed as a single zero-thickness surface with an
 * index that produces exactly the wanted power.
 *
 * It exists so the exposure tests have an optic with NO aberration and NO
 * dispersion: f/2 at 1/500 s must equal f/2.8 at 1/250 s to the last bit, and
 * on a real prescription it does not, because changing the aperture also
 * changes the vignetting and the aberration. Testing the exposure triangle on
 * a real lens would mean loosening the tolerance until the test could no
 * longer see a genuine error. */
static void build_thin(OsPrescription *p) {
    const ls_real f = 100.0;
    const ls_real n = 2.0;

    /* TWO surfaces, not one. A single surface whose following medium is air
     * has n = n' on both sides and therefore no power at all -- the first
     * version of this function did exactly that and produced a lens of
     * infinite focal length, which os_lens_build caught.
     *
     * So: a curved entry surface into a dispersionless medium of index n,
     * immediately followed by a plano exit back into air with zero thickness
     * between them. The curved surface has power (n - 1)/R, so R = (n - 1)f
     * gives exactly 1/f; the plano surface has zero power and only returns the
     * ray to air. Nothing accumulates, so the focal length is f to the last
     * bit at every wavelength. */
    p->name   = "thin";
    p->source = "ideal: dispersionless, aberration-free, exactly f = 100 mm";
    p->nsurf  = 2;
    p->stop_index = 0;
    p->design_efl_mm   = f;
    p->design_fno      = 1.0;
    p->image_circle_mm = 43.3;         /* covers full frame */

    p->surf[0] = (OsSurface){ .radius_mm = (n - 1.0) * f, .thickness_mm = 0.0,
                              .semi_ap_mm = 50.0, .glass = OS_GCONST(n),
                              .is_stop = true };
    p->surf[1] = (OsSurface){ .radius_mm = 0.0, .thickness_mm = 0.0,
                              .semi_ap_mm = 50.0, .glass = OS_GREF(OS_GLASS_AIR),
                              .is_stop = false };
}

bool os_prescription(OsPrescriptionId id, OsPrescription *out) {
    memset(out, 0, sizeof *out);
    switch (id) {
        case OS_LENS_THIN:          build_thin(out);     return true;
        case OS_LENS_SINGLET_100:   build_singlet(out);  return true;
        case OS_LENS_ACHROMAT_100:  build_achromat(out); return true;
        default:                    return false;
    }
}

const char *os_prescription_name(OsPrescriptionId id) {
    OsPrescription p;
    if (!os_prescription(id, &p)) return "?";
    return p.name;
}

void os_prescription_scale(OsPrescription *p, ls_real k) {
    /* Every length, or it is not a real design. The f-number is preserved
     * automatically: the focal length and the entrance pupil scale together,
     * and every angle in the system is untouched. */
    for (int i = 0; i < p->nsurf; ++i) {
        p->surf[i].radius_mm    *= k;   /* 0 stays 0: a plano stays plano */
        p->surf[i].thickness_mm *= k;
        p->surf[i].semi_ap_mm   *= k;
    }
    p->design_efl_mm   *= k;
    p->image_circle_mm *= k;
    /* design_fno is deliberately NOT scaled. That is the whole point. */
}
