/* ui.h — the toolbar's model: layout, hit-testing, and every enable rule.
 *
 * THE INVARIANT THIS MODULE OWNS
 *   A hotkey and the button beside it can never disagree, because they are the
 *   same rule. Every command -- typed or clicked -- is a UiAction, every
 *   action's availability is decided by the one switch in ui_apply_state(),
 *   and a command that is refused explains itself using its own tooltip text
 *   rather than failing silently.
 *
 *   Free of SDL, so the layout arithmetic and the enable rules are exercised
 *   headlessly by the test suite. check-sdl-purity in the Makefile keeps it
 *   that way. viewer/draw.c only draws what this produces.
 *
 * WHY A TABLE
 *   Label, hotkey hint, hover text and grouping live together in one
 *   BUTTON_SPECS array. Splitting them across a layout function, a draw
 *   function and a key handler is how a button ends up with no tooltip, or a
 *   hotkey that does something the button cannot -- both of which the tests
 *   here assert against directly.
 */
#ifndef OPTICSIM_VIEWER_UI_H
#define OPTICSIM_VIEWER_UI_H

#include <stdbool.h>

/* Layout constants, matching the sibling repos so the three look like one
 * family of tools. */
#define UI_TOOLBAR_W    148
#define UI_BUTTON_H      26
#define UI_BUTTON_MIN_H  16     /* how far buttons squeeze to fit a short window */
#define UI_BUTTON_GAP     4
#define UI_GROUP_GAP     10
#define UI_MARGIN        10
#define UI_TOP_MARGIN    12

typedef enum {
    UI_NONE = 0,
    /* what the canvas shows */
    UI_VIEW_SCENE, UI_VIEW_LENS, UI_VIEW_IMAGE,
    /* the handful of settings worth a dedicated button; everything else,
     * including these, is editable in the inspector panel */
    UI_LENS_NEXT,
    UI_OPEN_UP, UI_STOP_DOWN,
    UI_FOCUS_NEAR, UI_FOCUS_FAR,
    UI_BLADES,
    /* diagram overlays */
    UI_RAYS,
    UI_COLOUR,
    UI_SPOT,
    UI_GRID,
    /* arranging the scene */
    UI_ADD_OBJECT, UI_ADD_LIGHT, UI_LIGHT_MODE, UI_DELETE, UI_SELECT_NEXT,
    UI_UNDO, UI_REDO,
    /* the rendered image */
    UI_SAVE,
    UI_RESET,
    UI_HELP,
    UI_ACTION_COUNT
} UiAction;

typedef struct { int x, y, w, h; } UiRect;

typedef struct {
    UiAction    action;
    const char *label;
    const char *hint;        /* hotkey reminder, drawn dim and right-aligned  */
    const char *tip;         /* what it does, and what it needs -- also the   */
                             /* message shown when the command is refused     */
    bool        group_start;
    UiRect      rect;
    bool        enabled;
    bool        active;
} UiButton;

/* Headroom, deliberately. ui_init CLAMPS to this rather than failing, so a
 * table that outgrows it loses its last buttons with nothing said -- and the
 * last buttons are the newest ones, which is exactly when nobody is looking
 * for a missing control. */
#define UI_MAX_BUTTONS 32

typedef struct {
    UiButton buttons[UI_MAX_BUTTONS];
    int      count;
    int      hover;          /* index, or -1 */
    int      pressed;        /* index, or -1 */
} Toolbar;

/* A flat snapshot of everything the enable rules may look at.
 *
 * Deliberately a POD of scalars rather than a pointer to the app: ui.c must
 * not be able to reach into the model, or the rules drift toward reading
 * whatever is convenient and stop being testable in isolation. */
typedef struct {
    int   view;              /* OsViewMode */
    bool  rendering;         /* the image view has a render in flight */
    bool  has_selection;
    bool  ambient;           /* lit by the dome rather than by the lamps */
    bool  can_undo, can_redo;
    bool  room_for_object, room_for_light;
    int   scene_items;       /* live objects + lights, for SELECT NEXT */
    bool  showing_rays;
    bool  showing_spot;
    bool  showing_grid;
    bool  chromatic;         /* drawing three wavelengths rather than one   */
    int   blades;
    int   lens_index;
    int   lens_count;
    double focal_mm, focal_min_mm, focal_max_mm;
    double fno, fno_min, fno_max;
    double focus_m, focus_min_m, focus_max_m;
    bool  helping;
} UiState;

void        ui_init(Toolbar *t, int strip_height);
void        ui_apply_state(Toolbar *t, UiState s);
int         ui_hit(const Toolbar *t, int x, int y);   /* index, or -1 */
bool        ui_action_enabled(const Toolbar *t, UiAction a);
const char *ui_action_label(const Toolbar *t, UiAction a);
const char *ui_action_tip(const Toolbar *t, UiAction a);

/* Hotkey -> action. Lowercase ASCII; returns UI_NONE for anything unbound.
 * Kept here, next to the table, so a key and its button cannot drift apart. */
UiAction    ui_action_for_key(char c);

#endif /* OPTICSIM_VIEWER_UI_H */
