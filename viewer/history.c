/* history.c — the two stacks. See history.h for why they hold snapshots. */
#include "history.h"

#include <stdlib.h>
#include <string.h>

bool os_history_init(OsHistory *h) {
    memset(h, 0, sizeof *h);
    h->undo = calloc(OS_HISTORY_DEPTH, sizeof *h->undo);
    h->redo = calloc(OS_HISTORY_DEPTH, sizeof *h->redo);
    if (!h->undo || !h->redo) { os_history_free(h); return false; }
    return true;
}

void os_history_free(OsHistory *h) {
    free(h->undo); h->undo = NULL;
    free(h->redo); h->redo = NULL;
    h->nundo = h->nredo = 0;
}

/* Push onto a stack that is allowed to be full, dropping the oldest entry.
 *
 * The shift is a memmove of the whole stack -- about 340 KB at the depth
 * limit, tens of microseconds. That is once per completed gesture, at human
 * speeds, and it buys an array whose newest entry is always at the top with
 * no wrap arithmetic anywhere. A ring buffer would avoid the copy and put a
 * modulo in every access; this is the cheaper mistake to not make. */
static void push(OsSettings *stack, int *n, const OsSettings *s) {
    if (*n >= OS_HISTORY_DEPTH) {
        memmove(&stack[0], &stack[1],
                (size_t)(OS_HISTORY_DEPTH - 1) * sizeof *stack);
        *n = OS_HISTORY_DEPTH - 1;
    }
    stack[(*n)++] = *s;
}

bool os_history_record(OsHistory *h, const OsSettings *before,
                       const OsSettings *after) {
    if (!h->undo || !h->redo) return false;
    if (!os_settings_doc_differs(before, after)) return false;

    push(h->undo, &h->nundo, before);
    /* A new edit after an undo abandons the branch that was stepped away
     * from. Keeping it would mean redo landing somewhere the edits since
     * cannot account for. */
    h->nredo = 0;
    return true;
}

bool os_history_undo(OsHistory *h, OsSettings *inout) {
    if (!h->undo || h->nundo <= 0) return false;
    push(h->redo, &h->nredo, inout);
    os_settings_restore_doc(inout, &h->undo[--h->nundo]);
    return true;
}

bool os_history_redo(OsHistory *h, OsSettings *inout) {
    if (!h->redo || h->nredo <= 0) return false;
    push(h->undo, &h->nundo, inout);
    os_settings_restore_doc(inout, &h->redo[--h->nredo]);
    return true;
}
