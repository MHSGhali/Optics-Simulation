/* prescription.h — lens designs as inert data.
 *
 * THE INVARIANT THIS MODULE OWNS
 *   A prescription is data and nothing else. It has no f-number, no focus
 *   distance and no chosen focal length -- those belong to an OsLens, which is
 *   a *mounted, stopped, focused instance* of a prescription. Nothing
 *   downstream of lens.h ever reads a prescription directly.
 *
 *   And: scaling a prescription by k multiplies EVERY length by k -- radii,
 *   thicknesses and semi-apertures alike -- leaving every angle in the system,
 *   and therefore the f-number and the aberration character, exactly
 *   unchanged. The failure mode that buys is a focal-length control that
 *   scales the radii but forgets the thicknesses, producing a lens that is
 *   neither the original design nor any other real design, and which still
 *   renders a perfectly plausible image.
 *
 * SIGN CONVENTION, STATED ONCE AND NOWHERE ELSE
 *   Light travels in +z. Surfaces are listed front (object side) first, rear
 *   (sensor side) last. z = 0 is the front vertex at construction time.
 *
 *       radius_mm > 0   centre of curvature lies at GREATER z than the vertex
 *       radius_mm < 0   centre of curvature lies at SMALLER z
 *       radius_mm = 0   plano
 *
 *   `glass` names the medium the ray enters AFTER crossing this surface, so
 *   the last surface is essentially always air. `thickness_mm` is the axial
 *   gap from this surface's vertex to the next one's.
 *
 *   Get the radius sign backwards and the lens focuses backwards -- and still
 *   renders an image, just an inverted, wrong one. That is why the convention
 *   is written here and why os_lens_build() checks the paraxial focal length
 *   against the design value before it will hand back a lens.
 *
 * WHAT IS SHIPPED, AND WHY THESE
 *   SINGLET_100 and ACHROMAT_100 are DERIVED from the thin-lens design
 *   equations at fetch time rather than transcribed as literals, so their
 *   radii are exact to the last bit instead of being hand-rounded. They are
 *   the test articles: the singlet's chromatic error must come out at -1/V_d
 *   and the doublet's must be two orders of magnitude smaller. That pair of
 *   results proves the glass table, the paraxial trace and the achromat
 *   derivation are all simultaneously right, and it is the reason a 100 mm
 *   doublet nobody would photograph with is the first lens in the file.
 *
 *   Real photographic prescriptions (double Gauss, Tessar, telephoto,
 *   retrofocus) land with the renderer. They are transcriptions, and published
 *   tables print only n_d per element -- never the glass name -- so each one
 *   needs its glasses sourced properly rather than guessed at from the nine
 *   catalogue entries that happen to exist today. Guessing would fix each
 *   element's index and get its ABBE NUMBER wrong, which is exactly backwards:
 *   the Abbe number is what decides colour correction, and colour correction
 *   is what this program exists to show.
 */
#ifndef OPTICSIM_PRESCRIPTION_H
#define OPTICSIM_PRESCRIPTION_H

#include "opticsim/glass.h"

#define OS_MAX_SURF 24

/* How a surface names its glass.
 *
 * Two forms, because prescriptions come from two places. Catalogue glasses are
 * named (`N-BK7`); transcribed designs give only (n_d, V_d) and expect the
 * reader to synthesise a glass matching both, which os_glass_model() does. */
typedef enum {
    OS_GREF_CATALOGUE,   /* a named glass: N-BK7                              */
    OS_GREF_MODEL,       /* synthesised to match a published (n_d, V_d) pair  */
    OS_GREF_CONSTANT     /* dispersionless, for the ideal thin lens only      */
} OsGlassRefKind;

typedef struct {
    OsGlassRefKind kind;
    OsGlassId      id;       /* CATALOGUE */
    double         nd, vd;   /* MODEL     */
    double         n;        /* CONSTANT  */
} OsGlassRef;

#define OS_GREF(glassid)    ((OsGlassRef){ .kind = OS_GREF_CATALOGUE, .id = (glassid) })
#define OS_GMODEL(nd_, vd_) ((OsGlassRef){ .kind = OS_GREF_MODEL, .nd = (nd_), .vd = (vd_) })
#define OS_GCONST(n_)       ((OsGlassRef){ .kind = OS_GREF_CONSTANT, .n = (n_) })

/* Resolve a reference to an actual glass. Returns false if a model glass
 * failed to converge -- never a silently substituted default, because a wrong
 * glass produces a working lens with the wrong colour behaviour. */
bool os_glassref_resolve(OsGlassRef ref, OsGlass *out);

typedef struct {
    ls_real    radius_mm;      /* signed, see the convention above; 0 = plano */
    ls_real    thickness_mm;   /* axial gap to the NEXT surface               */
    ls_real    semi_ap_mm;     /* clear semi-aperture: the radius, not the    */
                               /* diameter. This clip IS mechanical           */
                               /* vignetting; there is no other source of it. */
    OsGlassRef glass;          /* medium AFTER this surface                   */
    bool       is_stop;        /* the aperture stop; exactly one per design   */
} OsSurface;

typedef struct {
    const char *name;
    const char *source;        /* where the numbers came from, for the reader */
    OsSurface   surf[OS_MAX_SURF];
    int         nsurf;
    int         stop_index;    /* -1 if the design has no explicit stop       */
    ls_real     design_efl_mm; /* what the design is nominally; checked       */
    ls_real     design_fno;    /* widest aperture the design supports         */
    ls_real     image_circle_mm; /* diameter it covers at the design focal    */

    /* ---- a design may be a FAMILY rather than a fixed table ----
     *
     * A ZOOM is not one lens. Its groups sit at a separation the ring moves,
     * and every separation is a different design with a different focal
     * length, different aberrations and different distortion. So the surface
     * list becomes a function of one mechanical number, and `param` in
     * os_prescription() is that number.
     *
     * Fixed designs leave `parametric` false and are reached at any focal
     * length by SCALING, which is a real optical operation -- a scaled design
     * has identical angular behaviour -- and is why they carry no focal range.
     * A parametric one cannot be scaled: its separation already sets its focal
     * length, and it is only honest over the span its mechanism reaches. */
    bool        parametric;
    ls_real     param_min, param_max;    /* the mechanism's travel            */
    ls_real     focal_min_mm;            /* what that travel delivers, and    */
    ls_real     focal_max_mm;            /* 0 for a design that scales freely */
} OsPrescription;

typedef enum {
    OS_LENS_THIN = 0,      /* sphere into index 2, then a plano: exactly      */
                           /* 100 mm at every wavelength, for the exposure    */
                           /* tests. Ideal in COLOUR only -- one spherical    */
                           /* surface is not aplanatic, and this leaves more  */
                           /* spherical aberration than the achromat does     */
    OS_LENS_SINGLET_100,   /* equiconvex N-BK7, f = 100 mm: the chromatic     */
                           /* control -- it is SUPPOSED to fringe             */
    OS_LENS_ACHROMAT_100,  /* Fraunhofer N-BK7 + F2 doublet, f = 100 mm       */
    OS_LENS_ZOOM_RETRO,    /* a RETROFOCUS ZOOM: a negative doublet, the stop, */
                           /* a positive doublet. The separation sets the      */
                           /* focal length, so this is the one design where    */
                           /* the focal control moves glass rather than        */
                           /* rescaling a fixed lens. 45-100 mm.               */
    OS_LENS_COUNT
} OsPrescriptionId;

/* Fetch a prescription BY VALUE.
 *
 * By value, and recomputed per call, so there is no lazily initialised global
 * to make thread-safe and no pointer into a static for a caller to scribble
 * on. A prescription is under a kilobyte and is fetched once per lens build,
 * never in a render loop. */
bool os_prescription(OsPrescriptionId id, ls_real param, OsPrescription *out);

const char *os_prescription_name(OsPrescriptionId id);

/* Scale every length by k. See the invariant at the top: radii, thicknesses
 * AND semi-apertures, or the result is not a real design. */
void os_prescription_scale(OsPrescription *p, ls_real k);

#endif /* OPTICSIM_PRESCRIPTION_H */
