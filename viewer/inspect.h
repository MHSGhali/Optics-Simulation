/* inspect.h — every setting, as data.
 *
 * THE INVARIANT THIS MODULE OWNS
 *   There is exactly one place a setting can be changed, and one place its
 *   bounds live. A value typed in, a value dragged, and a value set by a
 *   toolbar button all go through os_inspect_set(), so none of them can
 *   produce a state the others cannot.
 *
 *   The failure mode that buys is a control that clamps in one path and not
 *   another -- an aperture that stops at f/1.0 when dragged but goes to f/0.2
 *   when typed, producing a lens the tracer cannot build and a blank window
 *   with no explanation.
 *
 * FREE OF SDL
 *   The field list and the edit semantics are exercised headlessly by the test
 *   suite; viewer/draw.c only draws what this produces and feeds edits back.
 *
 * MULTIPLICATIVE VERSUS LINEAR
 *   Focal length, aperture, focus distance, shutter and sample counts all span
 *   decades, and dragging them by a fixed increment is unusable at one end of
 *   the range and unusably coarse at the other. Those are marked
 *   `logarithmic`, and scrubbing them multiplies rather than adds -- which is
 *   also how a photographer thinks about every one of them: stops, not
 *   millimetres of aperture.
 */
#ifndef OPTICSIM_VIEWER_INSPECT_H
#define OPTICSIM_VIEWER_INSPECT_H

#include "opticsim/lens.h"
#include "opticsim/scenedesc.h"

typedef enum { OS_VIEW_SCENE, OS_VIEW_LENS, OS_VIEW_IMAGE,
               OS_VIEW_COUNT } OsViewMode;

/* Every setting the program has, in one struct.
 *
 * One struct rather than fields scattered through the app, because the render
 * has to restart whenever any of them changes, and comparing one struct is a
 * check that cannot be forgotten when a setting is added. */
typedef struct {
    /* lens */
    int     lens;                 /* OsPrescriptionId */
    double  focal_mm;
    double  fno;
    double  focus_m;
    int     blades;               /* 0 = perfect circle, else 3..14 */
    double  curvature;            /* 0 straight blades, 1 circular */
    double  rot_deg;

    /* scene */
    int     stage;                /* the preset this was last seeded from */
    OsSceneDesc scene;            /* the arrangement itself -- editable   */

    /* What the panel is editing. Held as ids into `scene`, and DELIBERATELY
     * excluded from os_settings_image_differs: selecting something must not
     * throw away a converged render. Mutually exclusive; -1 for none. */
    int     sel_obj;
    int     sel_light;

    /* sensor and image */
    double  sensor_w_mm;
    int     res_w;                /* render width; height follows the sensor */
    double  exposure;
    double  coc_limit_mm;         /* the blur that still counts as sharp */

    /* sampling */
    int     spp;                  /* per pass */
    int     depth;

    /* view */
    int     view;                 /* OsViewMode */
    bool    show_rays, show_spot, show_grid, chromatic;
} OsSettings;

void os_settings_default(OsSettings *s);
int  os_settings_res_h(const OsSettings *s);   /* from the sensor's aspect */

/* True if anything that affects the RENDERED IMAGE differs. View-only toggles
 * are excluded on purpose: turning the ray fan off must not throw away a
 * half-converged render. */
bool os_settings_image_differs(const OsSettings *a, const OsSettings *b);

/* ---- the document / viewport split, which is what undo operates on ----
 *
 * The DOCUMENT is everything that describes the photograph: the lens, the
 * arrangement, the sensor, the sampling, and what the panel is editing. The
 * VIEWPORT is which of the three views is on screen and which overlays are
 * drawn -- where you are standing, not what you made.
 *
 * Undo is about the document. An undo that also threw you into a different
 * view would be obeying the letter of the word and not its point.
 *
 * Both functions are written as "the whole struct, then the viewport back",
 * so that adding a SETTING needs no edit here and only adding a VIEW TOGGLE
 * does. That is the direction where forgetting is survivable: a missed toggle
 * merely becomes undoable, where a missed setting would silently not be. */
bool os_settings_doc_differs(const OsSettings *a, const OsSettings *b);
void os_settings_restore_doc(OsSettings *dst, const OsSettings *src);

typedef enum {
    FLD_NONE = 0,
    FLD_H_LENS, FLD_LENS, FLD_FOCAL, FLD_FNO, FLD_FOCUS,
    FLD_BLADES, FLD_CURVE, FLD_ROT,
    FLD_H_SCENE, FLD_STAGE, FLD_LIGHTING, FLD_AMB_LUX, FLD_AMB_CCT,
    FLD_H_OBJECT, FLD_O_KIND, FLD_O_X, FLD_O_Y, FLD_O_Z, FLD_O_RADIUS,
    FLD_O_R, FLD_O_G, FLD_O_B, FLD_O_DEPTH, FLD_O_SPOT,
    FLD_H_LIGHT, FLD_L_KIND, FLD_L_X, FLD_L_Y, FLD_L_Z,
    FLD_L_RADIUS, FLD_L_SIZEU, FLD_L_SIZEV,
    FLD_L_FLUX, FLD_L_CCT, FLD_L_WATTS, FLD_L_EFFICACY,
    FLD_H_SENSOR, FLD_SENSOR_W, FLD_RES, FLD_EXPOSURE, FLD_COC,
    FLD_H_SAMPLING, FLD_SPP, FLD_DEPTH,
    FLD_H_DERIVED, FLD_D_EFL, FLD_D_HFOV, FLD_D_FNO, FLD_D_EP, FLD_D_TSTOP,
    FLD_D_DISTORT,
    FLD_D_BFD, FLD_D_FILM, FLD_D_COLOUR, FLD_D_COC, FLD_D_COVER,
    FLD_D_NEAR, FLD_D_FAR, FLD_D_HYPER, FLD_D_SAMPLES,
    FLD_COUNT
} FieldId;

typedef struct {
    FieldId     id;
    const char *label;
    const char *unit;        /* "" when the label carries it */
    double      value;
    double      lo, hi;      /* clamped on set */
    bool        readonly;    /* derived: shown, never edited */
    bool        is_enum;
    bool        heading;     /* a section rule, not a value */
    bool        logarithmic; /* scrub multiplicatively */
    bool        integral;    /* whole numbers only: blades, bounces, pixels */
    const char *const *names;
    int         nnames;
} Field;

/* Enough for the camera rows PLUS the largest selection section. The light
 * section is the long one, and at 40 the derived depth-of-field rows fell off
 * the end of the list -- silently, because the builder just stops pushing. */
#define OS_INSPECT_MAX 56

/* Build the visible field list.
 *
 * The OBJECT and LIGHT sections appear only when something is selected, so the
 * panel shows what you are editing rather than everything at once. `L` and the
 * sample count supply the derived rows; pass NULL / 0 before a lens exists. */
int  os_inspect_fields(const OsSettings *s, const OsLens *L,
                       uint64_t samples_done, Field *out, int max);

/* Apply an edit, clamped. Returns true if the stored value actually moved, so
 * the caller knows whether to restart the render. */
bool os_inspect_set(OsSettings *s, FieldId id, double v);

/* One scrub step: `dx` pixels of drag on `f`. Multiplicative for the wide
 * ranges, linear otherwise. */
double os_inspect_scrub(const Field *f, double v, int dx);

/* ---- the focal range the CONTROL offers ----
 *
 * Here because inspect.h owns "one place a setting's bounds live", and this one
 * had drifted into three: the field list, os_inspect_set's clamp, and the
 * viewer's button limits. Three copies of a bound is the exact failure this
 * module's invariant exists to prevent -- a value that clamps when dragged and
 * not when typed.
 *
 * 2.5 mm is far shorter than the shipped designs can COVER: the prescriptions
 * are reached at other focal lengths by scaling, so a 100 mm doublet at 2.5 mm
 * covers a 0.5 mm image circle on a 43 mm frame, and all but the middle of the
 * picture is aberration. That is honest output rather than a reason to forbid
 * the setting -- os_lens_distortion_pct and the COVERS row say exactly how bad
 * it is, which is the point. A design with a range of its own (a zoom) clamps
 * tighter, through os_lens_design_focal_range. */
#define OS_FOCAL_MIN_MM   2.5
#define OS_FOCAL_MAX_MM 400.0

/* Would `c` be taken as part of a typed value for this field?
 *
 * THE ONE PLACE THAT DECIDES, and it has to be, because two callers depend on
 * agreeing exactly. SDL delivers SDL_KEYDOWN before SDL_TEXTINPUT for the same
 * press, so the key handler has to know a keystroke is bound for a text field
 * BEFORE the text handler has seen it. When those two tests were written
 * separately they drifted, and the difference leaked out as a hotkey: the first
 * character of every typed number also ran its shortcut, so typing 0.030 into
 * SHARP IF fired UI_RESET and rebuilt the scene. Only the first character --
 * everything after it was caught -- which is why 45 typed cleanly and 0.03
 * did not.
 *
 * A character is accepted only if it could legitimately belong to THIS field's
 * value, which keeps as many shortcuts alive as possible while a row is
 * selected: '.' stays UI_FOCUS_FAR on an integer row, and '-' stays UI_OPEN_UP
 * on a row that cannot go negative. */
bool os_inspect_accepts_char(const Field *f, char c);

void os_inspect_format(const Field *f, char *buf, size_t n);

#endif /* OPTICSIM_VIEWER_INSPECT_H */
