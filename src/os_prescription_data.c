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

/* An ideal lens: exactly the wanted focal length, at every wavelength.
 *
 * IDEAL IN COLOUR, NOT IN ITS RAYS. It exists so the exposure tests have an
 * optic with no DISPERSION -- f/2 at 1/500 s must equal f/2.8 at 1/250 s to the
 * last bit, and on a real prescription it does not, because changing the
 * aperture also changes the vignetting and the aberration. What it does NOT
 * have is freedom from aberration, and two comments here used to say it did. A
 * single spherical surface is not aplanatic: at f/5 this leaves MORE spherical
 * aberration than the achromat does, because the doublet's second element bends
 * the marginal rays back and this has nothing to do that with. The tests pin
 * that, so nobody restores the claim.
 *
 * TWO surfaces, not one. A single surface whose following medium is air has
 * n = n' on both sides and therefore no power at all -- the first version of
 * this function did exactly that and produced a lens of infinite focal length,
 * which os_lens_build caught.
 *
 * So: a curved entry surface into a dispersionless medium of index n, followed
 * by a plano exit back into air. The curved surface has power (n - 1)/R, so
 * R = (n - 1)f gives exactly 1/f; the plano has zero power and only returns the
 * ray to air. */
static void build_thin(OsPrescription *p) {
    const ls_real f = 100.0;
    const ls_real n = 2.0;

    /* AND THEY ARE 20 mm APART, which they were not, and the gap is the whole
     * difference between a lens that renders and a lens that renders black.
     *
     * The two surfaces used to share a vertex, on the argument that nothing
     * then accumulates between them. That is true of the POWER and false of
     * the GEOMETRY: the R = 100 sphere's cap bulges 13.4 mm past its own vertex
     * at the 50 mm clear semi-aperture, so a plano at z = 0 sits INSIDE the
     * sphere it is supposed to follow. A sequential tracer visits surfaces in
     * prescription ORDER, not in hit order, so a camera ray reverse-traced from
     * the film cleared the plano and then found the sphere BEHIND it --
     * os_surface_hit wants t > 0 and got neither root positive. Every ray came
     * back vignetted and this design rendered pure black, in the viewer and
     * from the CLI both, with no error to say so.
     *
     * It survived because nothing in this program rendered through it. It is
     * the paraxial and exposure reference, and those use only the y-nu trace;
     * the browser port of this code hit it on the first frame, because there
     * IDEAL is one of three designs a visitor can pick.
     *
     * The gap costs nothing that matters. A plano contributes zero power at ANY
     * thickness, so the combined power stays phi1 + 0 - phi1*0*t/n = phi1 and
     * the focal length is still exactly 100.000000000 mm at every wavelength,
     * which is the whole point of this design. What moves is the BACK FOCAL
     * DISTANCE, 100 -> 90, and the rear principal plane, 0 -> -10. Those are
     * positions, not properties -- but they are the reason this lens is no
     * longer the textbook THIN lens for the purposes of a circle of confusion,
     * and the three tests that need that limit collapse a copy to get it.
     *
     * 20 mm rather than 14: the sag has to be cleared at the FULL semi-aperture
     * for the ray fan the lens view draws, not merely at the stopped-down one
     * a render happens to use. */
    p->name   = "thin";
    p->source = "ideal: dispersionless, exactly f = 100 mm";
    p->nsurf  = 2;
    p->stop_index = 0;
    p->design_efl_mm   = f;
    p->design_fno      = 1.0;
    p->image_circle_mm = 43.3;         /* covers full frame */

    p->surf[0] = (OsSurface){ .radius_mm = (n - 1.0) * f, .thickness_mm = 20.0,
                              .semi_ap_mm = 50.0, .glass = OS_GCONST(n),
                              .is_stop = true };
    p->surf[1] = (OsSurface){ .radius_mm = 0.0, .thickness_mm = 0.0,
                              .semi_ap_mm = 50.0, .glass = OS_GREF(OS_GLASS_AIR),
                              .is_stop = false };
}

/* ---- the retrofocus zoom -------------------------------------------------
 *
 * THE ONE DESIGN WHERE THE FOCAL CONTROL MOVES GLASS. Every other prescription
 * here is reached at other focal lengths by SCALING -- multiply every length by
 * k and you have a real lens of the same form, with identical angles and
 * therefore identical aberrations. That is a genuine optical operation and not
 * a shortcut, but it is not what a zoom ring does. A zoom changes the
 * SEPARATION between its groups, and every separation is a different lens.
 *
 * Two groups, each an achromatic doublet by the same Fraunhofer condition the
 * ACHROMAT uses, with the stop between them:
 *
 *     negative doublet  f = -60 mm     the group that makes it retrofocus
 *     the stop
 *     positive doublet  f = +50 mm     the group that forms the image
 *
 * For thin groups of powers phi1 and phi2 separated by t,
 *
 *     phi = phi1 + phi2 - phi1*phi2*t
 *
 * so t and the focal length trade off directly. The groups here are NOT thin --
 * 6.5 mm of glass each -- so that formula is the intuition and not the answer:
 * at t = 20 it predicts 100 mm where the real y-nu trace measures 86. The
 * separation is therefore SOLVED for, in os_lens_build, against the paraxial
 * trace itself. Which is also the honest model of the mechanism: a zoom cam is
 * cut to deliver a focal length, not to satisfy a thin-lens identity.
 *
 * WHY THE STOP SITS BETWEEN THE GROUPS, and what it costs
 *   Distortion is driven by stop position: a stop displaced from a lens gives
 *   barrel on one side and pincushion on the other, and the further it sits the
 *   more of it. Behind a NEGATIVE front group means barrel, and lots.
 *
 *   Measured across the travel, at the frame corner: -4.1 % at 100 mm, -6.9 %
 *   at 80 mm, -12.8 % at 60 mm, -23.4 % at 45 mm. Nearly a sixfold sweep from
 *   one design, which is the whole reason this lens is worth having -- the
 *   DISTORTION row moves as you zoom, and the GRID stage shows it bending.
 *
 * THE RANGE IS 45 TO 100 MM, AND BOTH ENDS ARE MEASURED
 *   The LONG end is the mechanism: bring the groups closer than 6 mm and their
 *   glass collides, and at that separation the trace measures 102.7 mm. 100 is
 *   inside it with room.
 *
 *   The WIDE end is the optics, and it is the interesting one. The entrance
 *   pupil of a retrofocus sits BEHIND its front element -- 27 mm behind,
 *   here -- so the front element's clear aperture caps the field angle at
 *   atan(24.7/26.9) = 42 degrees, and as the groups separate the frame corner
 *   demands more than that. Measured by tracing the corner's chief ray: it
 *   gets through at 45 mm and is vignetted at 40.
 *
 *   Splitting the front group into several weaker doublets does NOT help, which
 *   is worth knowing because it is the obvious thing to try: each element's
 *   radius grows with the split, so its aperture can, but the extra elements
 *   push the front vertex further from the pupil by the same proportion. The
 *   ratio that sets the field does not move. A real wide-angle solves this by
 *   pulling the pupil forward with a multi-element negative group whose
 *   bendings are OPTIMISED, not derived -- which is the honest reason there is
 *   no fisheye in this file. */

/* One achromatic doublet, by the Fraunhofer condition. The crown is equiconvex
 * (a free choice -- the condition fixes the powers, not the shapes) and the
 * flint's front radius follows so the two can be cemented. Works for a
 * NEGATIVE f as readily as a positive one: the powers simply change sign, and
 * the crown becomes the negative element. */
/* How far a surface reaches from its own vertex at height h, signed along +z.
 * The same sag os_lens_build's interpenetration gate measures. */
static ls_real sag_at(ls_real radius_mm, ls_real h) {
    if (fabs(radius_mm) < 1e-12) return 0.0;
    ls_real r2 = radius_mm * radius_mm, h2 = h * h;
    ls_real root = h2 >= r2 ? 0.0 : sqrt(r2 - h2);
    return radius_mm > 0.0 ? radius_mm - root : radius_mm + root;
}

/* The centre thickness an element needs so its two faces do not cross inside
 * its own clear aperture.
 *
 * THE STEP A THIN-LENS DERIVATION LEAVES OUT, and it is not a detail. Powers
 * and radii come from the design equations; thickness does not, and a
 * biconvex element 4 mm thick on a 22 mm radius has its two faces meeting
 * 12 mm from the axis. That lens cannot be built and cannot be traced -- the
 * sequential tracer reports every ray vignetted and renders black -- which is
 * exactly what os_lens_build's geometry gate caught when this design was first
 * written with the ACHROMAT's thicknesses copied across.
 *
 * `edge_mm` is the glass left at the rim. Real elements are not knife-edged;
 * they need a land to mount against. */
static ls_real centre_thickness(ls_real r_front, ls_real r_rear,
                                ls_real semi_ap, ls_real edge_mm) {
    ls_real t = edge_mm + sag_at(r_front, semi_ap) - sag_at(r_rear, semi_ap);
    return t > edge_mm ? t : edge_mm;
}

static int derive_doublet(OsSurface *s, ls_real f, ls_real semi_ap,
                          ls_real gap_after) {
    OsGlass crown = *os_glass(OS_GLASS_N_BK7);
    OsGlass flint = *os_glass(OS_GLASS_F2);
    ls_real v1 = os_glass_abbe(&crown), v2 = os_glass_abbe(&flint);
    ls_real n1 = os_glass_n(&crown, OS_LINE_D), n2 = os_glass_n(&flint, OS_LINE_D);

    ls_real phi  = 1.0 / f;
    ls_real phi1 =  phi * v1 / (v1 - v2);
    ls_real phi2 = -phi * v2 / (v1 - v2);
    ls_real R1 = 2.0 * (n1 - 1.0) / phi1;
    ls_real R2 = -R1;
    ls_real R3 = 1.0 / (1.0 / R2 - phi2 / (n2 - 1.0));

    /* Thicknesses follow from the radii and the aperture, not from taste. */
    ls_real tc = centre_thickness(R1, R2, semi_ap, 2.0);
    ls_real tf = centre_thickness(R2, R3, semi_ap, 2.0);

    s[0] = (OsSurface){ .radius_mm = R1, .thickness_mm = tc,
                        .semi_ap_mm = semi_ap, .glass = OS_GREF(OS_GLASS_N_BK7) };
    s[1] = (OsSurface){ .radius_mm = R2, .thickness_mm = tf,
                        .semi_ap_mm = semi_ap, .glass = OS_GREF(OS_GLASS_F2) };
    s[2] = (OsSurface){ .radius_mm = R3, .thickness_mm = gap_after,
                        .semi_ap_mm = semi_ap, .glass = OS_GREF(OS_GLASS_AIR) };
    return 3;
}

static void build_zoom_retro(OsPrescription *p, ls_real sep_mm) {
    /* The travel, and what it delivers. Both measured against the real y-nu
     * trace rather than the thin-lens identity; see the note above. */
    p->parametric   = true;
    p->param_min    = 6.0;
    p->param_max    = 54.0;
    p->focal_min_mm = 45.0;
    p->focal_max_mm = 100.0;

    ls_real t = sep_mm;
    if (t < p->param_min) t = p->param_min;
    if (t > p->param_max) t = p->param_max;

    p->name   = "zoom45-100";
    p->source = "derived: retrofocus pair of Fraunhofer doublets, "
                "separation sets the focal length";
    /* The nominal the transcription gate checks against. A family has no single
     * focal length, so the gate is told what THIS separation is meant to give
     * -- which os_lens_build has just solved for, so agreement is the check
     * that the solve converged rather than a transcription check. */
    p->design_fno       = 5.6;
    p->image_circle_mm  = 44.0;      /* covers full frame over its own range */
    p->stop_index       = 3;

    int n = 0;
    n += derive_doublet(&p->surf[n], -60.0, 24.7, t * 0.5);
    p->surf[n] = (OsSurface){ .radius_mm = 0.0, .thickness_mm = t * 0.5,
                              .semi_ap_mm = 18.0, .glass = OS_GREF(OS_GLASS_AIR),
                              .is_stop = true };
    n += 1;
    n += derive_doublet(&p->surf[n], 50.0, 20.0, 0.0);
    p->nsurf = n;

    /* Measured at this separation, by the caller's own paraxial trace -- so the
     * gate below compares the solve against the trace and not against a
     * remembered number. */
    p->design_efl_mm = 0.0;          /* filled in by os_lens_build */
}

bool os_prescription(OsPrescriptionId id, ls_real param, OsPrescription *out) {
    memset(out, 0, sizeof *out);
    switch (id) {
        case OS_LENS_THIN:          build_thin(out);     return true;
        case OS_LENS_SINGLET_100:   build_singlet(out);  return true;
        case OS_LENS_ACHROMAT_100:  build_achromat(out); return true;
        /* The only id that reads `param`: for a family, it is the separation
         * the zoom ring has moved the groups to. */
        case OS_LENS_ZOOM_RETRO:    build_zoom_retro(out, param); return true;
        default:                    return false;
    }
}

const char *os_prescription_name(OsPrescriptionId id) {
    OsPrescription p;
    if (!os_prescription(id, 0.0, &p)) return "?";
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
