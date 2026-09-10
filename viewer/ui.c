/* ui.c — the button table, the layout, and the one switch that holds every
 * enable rule. See ui.h for why they live together. */
#include "ui.h"
#include "inspect.h"

#include <string.h>

/* Label, hotkey, hover text and grouping, all in one place.
 *
 * The tip is written to work in two contexts, because it is used in both: as
 * hover help, and as the message shown when a command is refused. So it says
 * what the command does AND what it needs, rather than only naming it. */
static const struct {
    UiAction    action;
    const char *label;
    const char *hint;
    const char *tip;
    bool        group_start;
} BUTTON_SPECS[] = {
    { UI_VIEW_SCENE, "SCENE",    "1",
      "Look at the camera and its subjects from outside: what it can see, the "
      "plane it is focused on, and how much of the world either side of that "
      "is still sharp. Drag to orbit.", false },
    { UI_VIEW_LENS,  "LENS VIEW","2",
      "Show the lens in cross-section, with the light traced through it.", false },
    { UI_VIEW_IMAGE, "IMAGE",    "3",
      "Show what the camera records. The render refines while you leave it "
      "alone and starts over whenever a setting changes.", false },

    { UI_LENS_NEXT,  "LENS",     "L",
      "Swap the mounted lens. The singlet fringes colour badly and the "
      "achromat corrects it -- mount each in turn to see the difference.", true },
    { UI_OPEN_UP,    "OPEN",     "-",
      "Open the aperture one third of a stop. Stops when the iris reaches the "
      "edge of the glass -- the widest the design can physically go.", false },
    { UI_STOP_DOWN,  "STOP DOWN","=",
      "Close the aperture one third of a stop. Watch the blur discs shrink "
      "and the out-of-focus targets sharpen.", false },
    { UI_FOCUS_NEAR, "FOCUS IN", ",",
      "Focus closer. The film moves away from the glass; the lens extends.", false },
    { UI_FOCUS_FAR,  "FOCUS OUT",".",
      "Focus further away, out to infinity.", false },
    { UI_BLADES,     "BLADES",   "B",
      "Cycle the iris between a perfect circle and 3 to 14 straight blades. "
      "Changes the shape of the aperture and nothing else -- the area, and so "
      "the exposure, is held exactly constant.", false },

    { UI_RAYS,       "RAYS",     "R",
      "Show or hide the traced ray fan in the lens view.", true },
    { UI_COLOUR,     "COLOUR",   "C",
      "Trace blue, yellow and red separately instead of one wavelength. The "
      "three cross the axis at different points, and that gap is longitudinal "
      "chromatic aberration.", false },
    { UI_SPOT,       "FOCUS PT", "F",
      "Mark where each ray actually crosses the axis, against the paraxial "
      "focus. The spread between them IS the aberration.", false },
    { UI_GRID,       "SCALE",    "G",
      "Show the millimetre scale along the optical axis.", false },

    { UI_ADD_OBJECT, "ADD OBJ",  "A",
      "Put a new sphere in front of the camera. Drag it in the scene view, or "
      "type its position into the panel.", true },
    { UI_ADD_LIGHT,  "ADD LAMP", "W",
      "Add a lamp. Its brightness is set in lumens -- the number printed on a "
      "real bulb -- and the panel shows the watts that come to. Needs LAMPS "
      "lighting; under AMBIENT the lamps are off.", false },
    { UI_LIGHT_MODE, "LIGHTING", "E",
      "Switch between the placed lamps and a uniform dome of light from every "
      "direction. The dome casts no shadows at all, which makes it the honest "
      "way to judge focus -- a hard shadow edge reads as sharpness whether the "
      "lens is focused or not.", false },
    { UI_DELETE,     "DELETE",   "X",
      "Remove whatever is selected. Ids of everything else are unaffected, so "
      "the panel keeps editing what it was editing.", false },
    { UI_SELECT_NEXT,"NEXT",     "TAB",
      "Step the selection through the objects and then the lamps, for when "
      "something is too small or too far away to click.", false },

    { UI_UNDO,       "UNDO",     "Z",
      "Take back the last change. A whole drag is one step, not one per "
      "pixel. Which view you are looking at is not a change and is never "
      "undone.", true },
    { UI_REDO,       "REDO",     "Y",
      "Put back what was undone. Making a new change after undoing abandons "
      "the redo trail, because there would no longer be a state for it to "
      "lead to.", false },

    { UI_SAVE,       "SAVE",     "S",
      "Write the rendered image to out/viewer.ppm, at the exposure on screen.", true },
    { UI_RESET,      "RESET",    "0",
      "Put every setting back to where it started, and recentre the view.", false },
    { UI_HELP,       "HELP",     "?",
      "List every key and what it does. Drag a row in the panel on the right "
      "to change it, or click it and type a number.", false },
};
static const int NSPECS = (int)(sizeof BUTTON_SPECS / sizeof BUTTON_SPECS[0]);

void ui_init(Toolbar *t, int strip_height) {
    memset(t, 0, sizeof *t);
    t->hover = -1;
    t->pressed = -1;
    t->count = NSPECS < UI_MAX_BUTTONS ? NSPECS : UI_MAX_BUTTONS;

    for (int i = 0; i < t->count; ++i) {
        t->buttons[i].action      = BUTTON_SPECS[i].action;
        t->buttons[i].label       = BUTTON_SPECS[i].label;
        t->buttons[i].hint        = BUTTON_SPECS[i].hint;
        t->buttons[i].tip         = BUTTON_SPECS[i].tip;
        t->buttons[i].group_start = BUTTON_SPECS[i].group_start;
    }

    /* Squeeze to fit. The gaps go first, then the button height, down to
     * UI_BUTTON_MIN_H. Without this the toolbar simply ran off the bottom of a
     * laptop screen and the last buttons became unreachable -- which looks
     * like the feature is missing rather than like a layout problem. */
    int groups = 0;
    for (int i = 1; i < t->count; ++i) if (t->buttons[i].group_start) groups++;

    int gap = UI_BUTTON_GAP, ggap = UI_GROUP_GAP, bh = UI_BUTTON_H;
    for (int pass = 0; pass < 64; ++pass) {
        int need = UI_TOP_MARGIN + t->count * bh + (t->count - 1) * gap
                 + groups * (ggap - gap) + UI_MARGIN;
        if (need <= strip_height) break;
        if (ggap > gap) { ggap--; continue; }
        if (gap > 1)    { gap--;  continue; }
        if (bh > UI_BUTTON_MIN_H) { bh--; continue; }
        break;
    }

    int y = UI_TOP_MARGIN;
    for (int i = 0; i < t->count; ++i) {
        if (i > 0) y += (t->buttons[i].group_start ? ggap : gap);
        t->buttons[i].rect = (UiRect){ UI_MARGIN, y,
                                       UI_TOOLBAR_W - 2 * UI_MARGIN, bh };
        y += bh;
    }
}

void ui_apply_state(Toolbar *t, UiState s) {
    for (int i = 0; i < t->count; ++i) {
        UiButton *b = &t->buttons[i];
        b->enabled = true;
        b->active  = false;

        switch (b->action) {
            case UI_VIEW_SCENE: b->active = (s.view == OS_VIEW_SCENE); break;
            case UI_VIEW_LENS:  b->active = (s.view == OS_VIEW_LENS);  break;
            case UI_VIEW_IMAGE: b->active = (s.view == OS_VIEW_IMAGE); break;

            case UI_LENS_NEXT:
                b->enabled = s.lens_count > 1;
                break;

            /* Opening up is DECREASING the f-number, which is the one place
             * this UI is counter-intuitive, so the labels say OPEN and STOP
             * DOWN rather than +/-. Both disable at their limit rather than
             * accepting a click and doing nothing: a control that does nothing
             * reads as a broken program, one that greys out reads as a limit. */
            case UI_OPEN_UP:
                b->enabled = s.fno > s.fno_min * (1.0 + 1e-9);
                break;
            case UI_STOP_DOWN:
                b->enabled = s.fno < s.fno_max * (1.0 - 1e-9);
                break;
            case UI_FOCUS_NEAR:
                b->enabled = s.focus_m > s.focus_min_m * (1.0 + 1e-9);
                break;
            case UI_FOCUS_FAR:
                b->enabled = s.focus_m < s.focus_max_m * (1.0 - 1e-9);
                break;

            case UI_BLADES:
                b->active = s.blades >= 3;
                break;

            /* The diagram overlays mean nothing while the canvas is showing a
             * photograph, so they grey out rather than silently doing nothing
             * to a view that is not on screen. */
            case UI_RAYS:
                b->enabled = (s.view == OS_VIEW_LENS);
                b->active  = s.showing_rays && s.view == OS_VIEW_LENS;
                break;
            case UI_COLOUR:
                b->enabled = (s.view == OS_VIEW_LENS);
                b->active  = s.chromatic && s.view == OS_VIEW_LENS;
                break;
            case UI_SPOT:
                b->enabled = (s.view == OS_VIEW_LENS) && s.showing_rays;
                b->active  = s.showing_spot && s.showing_rays && s.view == OS_VIEW_LENS;
                break;
            case UI_GRID:
                b->enabled = (s.view == OS_VIEW_LENS);
                b->active  = s.showing_grid && s.view == OS_VIEW_LENS;
                break;

            /* Arranging only makes sense where you can see the arrangement. */
            case UI_ADD_OBJECT:
                b->enabled = (s.view == OS_VIEW_SCENE) && s.room_for_object;
                break;
            case UI_ADD_LIGHT:
                /* A lamp added under AMBIENT would emit nothing, and a control
                 * that appears to work and does nothing is worse than one that
                 * greys out and says why. */
                b->enabled = (s.view == OS_VIEW_SCENE) && s.room_for_light
                             && !s.ambient;
                break;
            case UI_LIGHT_MODE:
                b->active = s.ambient;
                break;
            case UI_DELETE:
                b->enabled = s.has_selection;
                break;
            case UI_SELECT_NEXT:
                b->enabled = s.scene_items > 0;
                break;

            /* Grey rather than accept a click and do nothing: an empty stack
             * is a limit, and a control that silently does nothing reads as a
             * broken program. */
            case UI_UNDO: b->enabled = s.can_undo; break;
            case UI_REDO: b->enabled = s.can_redo; break;

            /* Nothing to save until there is a rendered image. */
            case UI_SAVE:
                b->enabled = (s.view == OS_VIEW_IMAGE) && s.rendering;
                break;

            case UI_HELP: b->active = s.helping; break;

            default: break;
        }
    }
}

int ui_hit(const Toolbar *t, int x, int y) {
    for (int i = 0; i < t->count; ++i) {
        const UiRect *r = &t->buttons[i].rect;
        if (x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h)
            return i;
    }
    return -1;
}

static const UiButton *find(const Toolbar *t, UiAction a) {
    for (int i = 0; i < t->count; ++i)
        if (t->buttons[i].action == a) return &t->buttons[i];
    return NULL;
}

bool ui_action_enabled(const Toolbar *t, UiAction a) {
    const UiButton *b = find(t, a);
    return b && b->enabled;
}

const char *ui_action_label(const Toolbar *t, UiAction a) {
    const UiButton *b = find(t, a);
    return b ? b->label : "";
}

const char *ui_action_tip(const Toolbar *t, UiAction a) {
    const UiButton *b = find(t, a);
    return b ? b->tip : "";
}

UiAction ui_action_for_key(char c) {
    switch (c) {
        case '1': return UI_VIEW_SCENE;
        case '2': return UI_VIEW_LENS;
        case '3': return UI_VIEW_IMAGE;
        case 'l': return UI_LENS_NEXT;
        case 's': return UI_SAVE;
        case 'a': return UI_ADD_OBJECT;
        case 'w': return UI_ADD_LIGHT;
        case 'x': return UI_DELETE;
        case '\t': return UI_SELECT_NEXT;
        case '0': return UI_RESET;
        case '-': return UI_OPEN_UP;
        case '=': return UI_STOP_DOWN;
        case ',': return UI_FOCUS_NEAR;
        case '.': return UI_FOCUS_FAR;
        case 'b': return UI_BLADES;
        case 'r': return UI_RAYS;
        case 'c': return UI_COLOUR;
        case 'f': return UI_SPOT;
        case 'g': return UI_GRID;
        case 'e': return UI_LIGHT_MODE;
        case 'z': return UI_UNDO;
        case 'y': return UI_REDO;
        case '?': case '/': return UI_HELP;
        default:  return UI_NONE;
    }
}
