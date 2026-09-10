/* history.h — undo and redo, as whole snapshots.
 *
 * WHY SNAPSHOTS AND NOT A COMMAND LOG
 *   The alternative -- record each edit as an inverse operation -- needs every
 *   mutation in the program to know how to undo itself, and stays correct only
 *   as long as every future one remembers to. The bug that produces is silent:
 *   one operation forgets, and undo appears to work while quietly leaving the
 *   scene in a state that never existed.
 *
 *   OsSettings was made a plain POD of scalars and fixed arrays precisely so
 *   this could be a struct copy -- see the note in scenedesc.h. A snapshot
 *   cannot be wrong about what it captured, because it captured all of it.
 *
 *   The cost is honest and bounded: 5 KB a step, so the two stacks together
 *   are about 670 KB. They are heap-allocated rather than living in the
 *   App, which is a stack local.
 *
 * WHAT A STEP IS
 *   One entry per GESTURE, not one per change. Dragging a sphere across the
 *   scene fires a settings change on every mouse-motion event; if each were a
 *   step, undoing a drag would take fifty presses and the feature would be
 *   useless. The caller marks the end of a gesture and this records at most
 *   one step for it. See history_mark() in main.c.
 *
 * THE DOCUMENT, NOT THE VIEWPORT
 *   Only settings that describe the PHOTOGRAPH are compared and restored.
 *   Which view is on screen, whether the ray fan is drawn -- those are where
 *   you are standing, not what you made, and an undo that threw you into a
 *   different view would be obeying the letter of the word and not its point.
 *   os_settings_doc_differs and os_settings_restore_doc in inspect.h draw
 *   that line, and this module never looks inside a snapshot itself.
 *
 * FREE OF SDL
 *   So the stack discipline -- what a redo does after a new edit, what
 *   happens at the depth limit -- is checkable headlessly. That behaviour is
 *   easy to get subtly wrong and impossible to eyeball.
 */
#ifndef OPTICSIM_VIEWER_HISTORY_H
#define OPTICSIM_VIEWER_HISTORY_H

#include "inspect.h"

/* How far back you can go. Beyond this the OLDEST step is dropped: a stack
 * that refused new steps when full would silently stop recording, which is
 * the one failure an undo feature must not have. */
#define OS_HISTORY_DEPTH 64

typedef struct {
    OsSettings *undo;   /* undo[nundo-1] is the state before the last change */
    OsSettings *redo;
    int         nundo, nredo;
} OsHistory;

/* False if the allocation failed, in which case the history is empty and
 * every call below is a safe no-op -- undo missing is better than a crash. */
bool os_history_init(OsHistory *h);
void os_history_free(OsHistory *h);

/* Record `before` as a step to come back to, but ONLY if `after` differs from
 * it in a way the document cares about. Returns true if a step was recorded.
 *
 * Recording unconditionally is the classic bug here: a gesture that changed
 * nothing -- a click that missed, a scrub of zero pixels -- would leave a step
 * on the stack, and undo would appear to do nothing at all. */
bool os_history_record(OsHistory *h, const OsSettings *before,
                       const OsSettings *after);

/* Step back / forward. `inout` is the live settings: its document half is
 * replaced and its viewport half is left alone. False when there is nothing
 * to step to. */
bool os_history_undo(OsHistory *h, OsSettings *inout);
bool os_history_redo(OsHistory *h, OsSettings *inout);

/* For grey-out rules, so a button that cannot do anything says so rather than
 * accepting the click. */
static inline bool os_history_can_undo(const OsHistory *h) { return h->nundo > 0; }
static inline bool os_history_can_redo(const OsHistory *h) { return h->nredo > 0; }

/* NOTE: there is deliberately no "clear". RESET is recorded like any other
 * change, so undo brings your scene back -- which is what someone who has
 * just pressed the wrong button wants, and the whole reason to have undo. */

#endif /* OPTICSIM_VIEWER_HISTORY_H */
