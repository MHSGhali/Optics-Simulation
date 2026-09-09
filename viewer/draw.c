/* draw.c — SDL drawing. See draw.h for the palette's provenance. */
#include "draw.h"
#include "font.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Linkage-Design's toolbar palette, value for value. */
const Col COL_BG        = {  20,  20,  24 };
const Col COL_PANEL     = {  30,  30,  34 };
const Col COL_RULE      = {  55,  57,  64 };
const Col COL_FACE      = {  45,  47,  52 };
const Col COL_FACE_HOT  = {  62,  65,  72 };
const Col COL_FACE_DOWN = {  78,  82,  92 };
const Col COL_FACE_OFF  = {  34,  35,  39 };
const Col COL_EDGE      = {  70,  73,  82 };
const Col COL_EDGE_OFF  = {  48,  49,  54 };
const Col COL_INK       = { 205, 210, 220 };
const Col COL_HEAD      = { 235, 238, 245 };
const Col COL_MUTED     = { 130, 136, 148 };
const Col COL_DIM       = {  92,  95, 102 };
const Col COL_ACCENT    = { 255, 225,  70 };
const Col COL_ACCENT_DIM= { 200, 180,  70 };
const Col COL_WARN      = { 240, 180,  90 };
const Col COL_ERR       = { 235, 110, 100 };

const Col COL_GLASS      = { 140, 190, 235 };
const Col COL_GLASS_FILL = {  44,  66,  88 };
const Col COL_STOP       = { 200, 175, 235 };
const Col COL_FILM       = { 120, 200, 160 };
const Col COL_AXIS       = {  74,  78,  88 };
const Col COL_RAY        = { 235, 210, 130 };
const Col COL_RAY_BLOCKED= { 120,  74,  74 };
const Col COL_RAY_F      = { 120, 160, 255 };   /* 486 nm, blue  */
const Col COL_RAY_D      = { 235, 225, 130 };   /* 588 nm, yellow*/
const Col COL_RAY_C      = { 240, 120, 110 };   /* 656 nm, red   */

Col draw_level_col(StatusLevel level) {
    switch (level) {
        case STATUS_WARN:  return COL_WARN;
        case STATUS_ERROR: return COL_ERR;
        default:           return COL_INK;
    }
}

Col draw_lambda_col(ls_real lambda_nm) {
    if (lambda_nm < 520.0) return COL_RAY_F;
    if (lambda_nm > 620.0) return COL_RAY_C;
    return COL_RAY_D;
}

/* ---- primitives ---- */

static void setcol(SDL_Renderer *r, Col c, Uint8 a) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, a);
}

void draw_rect_fill(SDL_Renderer *r, int x, int y, int w, int h, Col c, Uint8 a) {
    SDL_Rect rc = { x, y, w, h };
    setcol(r, c, a);
    SDL_RenderFillRect(r, &rc);
}

void draw_rect_line(SDL_Renderer *r, int x, int y, int w, int h, Col c, Uint8 a) {
    SDL_Rect rc = { x, y, w, h };
    setcol(r, c, a);
    SDL_RenderDrawRect(r, &rc);
}

void draw_line(SDL_Renderer *r, int x0, int y0, int x1, int y1, Col c, Uint8 a) {
    setcol(r, c, a);
    SDL_RenderDrawLine(r, x0, y0, x1, y1);
}

void draw_dashed(SDL_Renderer *r, int x0, int y0, int x1, int y1, Col c, Uint8 a) {
    /* A dashed line says "this is a construction, not a thing" -- the paraxial
     * focus and the film plane are both drawn this way so they do not read as
     * physical surfaces. */
    ls_real dx = x1 - x0, dy = y1 - y0;
    ls_real len = sqrt(dx * dx + dy * dy);
    if (len < 1.0) return;
    int steps = (int)(len / 6.0);
    if (steps < 1) steps = 1;
    setcol(r, c, a);
    for (int i = 0; i < steps; ++i) {
        if (i & 1) continue;
        ls_real t0 = (ls_real)i / (ls_real)steps, t1 = (ls_real)(i + 1) / (ls_real)steps;
        SDL_RenderDrawLine(r, (int)(x0 + dx * t0), (int)(y0 + dy * t0),
                              (int)(x0 + dx * t1), (int)(y0 + dy * t1));
    }
}

void draw_text(SDL_Renderer *r, int x, int y, int scale, const char *s, Col c, Uint8 a) {
    setcol(r, c, a);
    int cx = x;
    for (const char *p = s; *p; ++p) {
        for (int col = 0; col < FONT_W; ++col)
            for (int row = 0; row < FONT_H; ++row)
                if (font_pixel(*p, col, row)) {
                    SDL_Rect px = { cx + col * scale, y + row * scale, scale, scale };
                    SDL_RenderFillRect(r, &px);
                }
        cx += (FONT_W + 1) * scale;
    }
}

void draw_text_right(SDL_Renderer *r, int rx, int y, int scale, const char *s, Col c, Uint8 a) {
    draw_text(r, rx - font_text_width(s, scale), y, scale, s, c, a);
}

void draw_text_mid(SDL_Renderer *r, int mx, int y, int scale, const char *s, Col c, Uint8 a) {
    draw_text(r, mx - font_text_width(s, scale) / 2, y, scale, s, c, a);
}

/* ---- toolbar ---- */

void draw_toolbar(SDL_Renderer *r, const Toolbar *t, int strip_h) {
    draw_rect_fill(r, 0, 0, UI_TOOLBAR_W, strip_h, COL_PANEL, 255);
    draw_line(r, UI_TOOLBAR_W - 1, 0, UI_TOOLBAR_W - 1, strip_h, COL_RULE, 255);

    for (int i = 0; i < t->count; ++i) {
        const UiButton *b = &t->buttons[i];
        const UiRect *q = &b->rect;

        if (b->group_start && i > 0)
            draw_line(r, q->x + 8, q->y - UI_GROUP_GAP / 2,
                         q->x + q->w - 8, q->y - UI_GROUP_GAP / 2, COL_RULE, 255);

        Col face = COL_FACE;
        if (!b->enabled)            face = COL_FACE_OFF;
        else if (t->pressed == i)   face = COL_FACE_DOWN;
        else if (t->hover == i)     face = COL_FACE_HOT;
        draw_rect_fill(r, q->x, q->y, q->w, q->h, face, 255);

        /* A toggled-on button borrows the accent for its edge. State is never
         * signalled by colour alone -- the label dims too -- so the toolbar
         * still reads without colour vision. */
        Col edge = b->active && b->enabled ? COL_ACCENT
                 : b->enabled              ? COL_EDGE : COL_EDGE_OFF;
        draw_rect_line(r, q->x, q->y, q->w, q->h, edge, 255);

        Col ink = !b->enabled ? COL_DIM : b->active ? COL_ACCENT : COL_INK;
        Col hint = !b->enabled ? COL_EDGE_OFF : b->active ? COL_ACCENT_DIM : COL_MUTED;

        int ty = q->y + (q->h - FONT_H) / 2;
        draw_text(r, q->x + 6, ty, 1, b->label, ink, 255);
        if (b->hint && b->hint[0])
            draw_text_right(r, q->x + q->w - 6, ty, 1, b->hint, hint, 255);
    }
}

/* ---- wrapped text ---- */

#define WRAP_LINES 12
#define WRAP_CHARS 72

/* Width of the first `n` characters. font.h only offers the whole string, and
 * the wrapper needs to ask "would one more character overflow?". */
static int width_n(int n, int scale) {
    return n <= 0 ? 0 : n * (FONT_W + 1) * scale - scale;
}

static int wrap(int scale, int max_w, const char *text,
                char lines[][WRAP_CHARS], int max_lines, int *widest) {
    int n = 0, w = 0;
    const char *p = text;
    while (*p && n < max_lines) {
        int len = 0, last_space = -1;
        while (p[len] && len < WRAP_CHARS - 1) {
            if (p[len] == ' ') last_space = len;
            if (width_n(len + 1, scale) > max_w) break;
            len++;
        }
        if (p[len] && last_space > 0) len = last_space;
        memcpy(lines[n], p, (size_t)len);
        lines[n][len] = '\0';
        int lw = font_text_width(lines[n], scale);
        if (lw > w) w = lw;
        n++;
        p += len;
        while (*p == ' ') p++;
    }
    if (widest) *widest = w;
    return n;
}

void draw_tooltip(SDL_Renderer *r, UiRect anchor, const char *text,
                  int win_w, int win_h) {
    if (!text || !text[0]) return;
    char lines[WRAP_LINES][WRAP_CHARS];
    int widest = 0;
    int n = wrap(1, 260, text, lines, WRAP_LINES, &widest);
    if (n == 0) return;

    int pad = 6;
    int w = widest + pad * 2;
    int h = n * (FONT_H + 3) + pad * 2 - 3;
    int x = anchor.x + anchor.w + 8;
    int y = anchor.y;
    if (x + w > win_w) x = win_w - w - 4;
    if (y + h > win_h) y = win_h - h - 4;
    if (y < 2) y = 2;

    draw_rect_fill(r, x, y, w, h, COL_PANEL, 245);
    draw_rect_line(r, x, y, w, h, COL_RULE, 255);
    for (int i = 0; i < n; ++i)
        draw_text(r, x + pad, y + pad + i * (FONT_H + 3), 1, lines[i], COL_INK, 255);
}

void draw_status(SDL_Renderer *r, const StatusLog *log, SDL_Rect canvas, Uint32 now) {
    /* A condition that still holds goes across the top; things that merely
     * happened stack from the bottom, newest lowest and older lines dimmer. */
    if (log->sticky_set) {
        int h = FONT_H + 10;
        draw_rect_fill(r, canvas.x, canvas.y, canvas.w, h, COL_PANEL, 235);
        draw_line(r, canvas.x, canvas.y + h, canvas.x + canvas.w, canvas.y + h,
                  COL_RULE, 255);
        draw_text(r, canvas.x + 10, canvas.y + 5, 1, log->sticky,
                  draw_level_col(log->sticky_level), 255);
    }

    const StatusMessage *vis[STATUS_HISTORY];
    int n = status_visible(log, (unsigned)now, vis, STATUS_HISTORY);
    int y = canvas.y + canvas.h - 8 - FONT_H;
    for (int i = 0; i < n; ++i) {
        /* Newest lowest, older lines dimmer, so the eye lands on the most
         * recent thing without having to read the stack. */
        int alpha = status_alpha(vis[i], (unsigned)now);
        if (alpha <= 0) continue;
        int dim = alpha - i * 55;
        if (dim < 40) dim = 40;
        draw_text(r, canvas.x + 10, y, 1, vis[i]->text,
                  draw_level_col(vis[i]->level), (Uint8)dim);
        y -= FONT_H + 4;
    }
}

void draw_help(SDL_Renderer *r, const Toolbar *t, SDL_Rect canvas) {
    /* Generated from the toolbar, so it cannot list a key that does not exist
     * or miss one that does. */
    int pad = 18;
    int w = 460, h = (t->count + 4) * (FONT_H + 6) + pad * 2;
    if (w > canvas.w - 20) w = canvas.w - 20;
    if (h > canvas.h - 20) h = canvas.h - 20;
    int x = canvas.x + (canvas.w - w) / 2;
    int y = canvas.y + (canvas.h - h) / 2;

    draw_rect_fill(r, x, y, w, h, COL_PANEL, 248);
    draw_rect_line(r, x, y, w, h, COL_RULE, 255);

    int ty = y + pad;
    draw_text(r, x + pad, ty, 2, "KEYS", COL_HEAD, 255);
    ty += FONT_H * 2 + 10;

    for (int i = 0; i < t->count; ++i) {
        const UiButton *b = &t->buttons[i];
        draw_text(r, x + pad, ty, 1, b->hint ? b->hint : "", COL_ACCENT, 255);
        draw_text(r, x + pad + 34, ty, 1, b->label, COL_INK, 255);
        ty += FONT_H + 6;
    }
    ty += 6;
    draw_text(r, x + pad, ty, 1, "DRAG", COL_ACCENT, 255);
    draw_text(r, x + pad + 34, ty, 1, "PAN     WHEEL  ZOOM", COL_MUTED, 255);
    ty += FONT_H + 6;
    draw_text(r, x + pad, ty, 1, "ESC", COL_ACCENT, 255);
    draw_text(r, x + pad + 34, ty, 1, "CLOSE THIS, THEN QUIT", COL_MUTED, 255);
}

/* ---- the cross-section ---- */

void draw_lensplot(SDL_Renderer *r, const LensPlot *lp, SDL_Rect canvas,
                   bool show_rays, bool show_spot, bool show_grid) {
    /* Fit the millimetre extents into the panel, ISOTROPICALLY. Independent x
     * and y scales would fill the panel better and would draw every lens as
     * the wrong shape -- and a lens diagram whose curvatures are wrong is
     * worse than no diagram. */
    ls_real zspan = lp->z_max - lp->z_min;
    ls_real xspan = 2.0 * lp->x_max;
    if (zspan <= 0.0 || xspan <= 0.0) return;

    ls_real sx = (canvas.w - 40) / zspan;
    ls_real sy = (canvas.h - 40) / xspan;
    ls_real s = sx < sy ? sx : sy;

    ls_real cx = canvas.x + canvas.w * 0.5;
    ls_real cy = canvas.y + canvas.h * 0.5;
    ls_real z0 = 0.5 * (lp->z_min + lp->z_max);

    #define PX(zz) ((int)(cx + ((zz) - z0) * s))
    #define PY(xx) ((int)(cy - (xx) * s))

    /* The optical axis. */
    draw_line(r, canvas.x + 6, PY(0.0), canvas.x + canvas.w - 6, PY(0.0),
              COL_AXIS, 255);

    if (show_grid) {
        /* A millimetre scale that picks a round step for the current zoom, so
         * the labels stay readable instead of colliding as you zoom out. */
        ls_real step = 1.0;
        while (step * s < 42.0) step *= (step < 2.0 ? 2.5 : 2.0);
        ls_real start = ceil(lp->z_min / step) * step;
        for (ls_real z = start; z <= lp->z_max; z += step) {
            int px = PX(z);
            if (px < canvas.x + 4 || px > canvas.x + canvas.w - 4) continue;
            draw_line(r, px, PY(0.0) - 4, px, PY(0.0) + 4, COL_AXIS, 255);
            char buf[32];
            snprintf(buf, sizeof buf, "%g", (double)z);
            draw_text_mid(r, px, PY(0.0) + 8, 1, buf, COL_DIM, 255);
        }
    }

    /* ---- rays, drawn under the glass so the glass reads as solid ---- */
    if (show_rays) {
        for (int i = 0; i < lp->nrays; ++i) {
            const LpRay *ray = &lp->ray[i];
            Col c = ray->blocked ? COL_RAY_BLOCKED : draw_lambda_col(ray->lambda_nm);
            Uint8 a = ray->blocked ? 150 : 220;
            for (int j = 0; j + 1 < ray->n; ++j)
                draw_line(r, PX(ray->z[j]), PY(ray->x[j]),
                             PX(ray->z[j + 1]), PY(ray->x[j + 1]), c, a);

            /* A clipped ray gets a tick where it died, so vignetting is
             * visible as an event rather than as an absence. */
            if (ray->blocked && ray->n > 0) {
                int px = PX(ray->z[ray->n - 1]), py = PY(ray->x[ray->n - 1]);
                draw_line(r, px - 3, py - 3, px + 3, py + 3, COL_ERR, 220);
                draw_line(r, px - 3, py + 3, px + 3, py - 3, COL_ERR, 220);
            }
        }
    }

    /* ---- the glass ----
     *
     * Element BODIES first, then every surface outline on top. Drawing only
     * the arcs, as the first version did, leaves a thin doublet almost
     * invisible against the ray fan -- and an optical layout whose glass you
     * cannot see is not doing its job. The two profiles bounding an element
     * share their transverse samples, so filling between them is one
     * horizontal span per sample. */
    for (int i = 0; i + 1 < lp->nprofiles; ++i) {
        const LpProfile *p = &lp->profile[i], *q = &lp->profile[i + 1];
        if (!p->solid_after) continue;
        int n = p->n < q->n ? p->n : q->n;
        for (int j = 0; j < n; ++j) {
            int y = PY(p->x[j]);
            int x0 = PX(p->z[j]), x1 = PX(q->z[j]);
            if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
            draw_rect_fill(r, x0, y, (x1 - x0) + 1, 2, COL_GLASS_FILL, 235);
        }
    }
    for (int i = 0; i < lp->nprofiles; ++i) {
        const LpProfile *p = &lp->profile[i];
        if (p->is_stop) continue;
        for (int j = 0; j + 1 < p->n; ++j)
            draw_line(r, PX(p->z[j]), PY(p->x[j]),
                         PX(p->z[j + 1]), PY(p->x[j + 1]), COL_GLASS, 255);
        /* Close the element's rim, so the edge reads as ground glass rather
         * than as an arc floating in space. */
        if (i + 1 < lp->nprofiles && p->solid_after) {
            const LpProfile *q = &lp->profile[i + 1];
            draw_line(r, PX(p->z[0]), PY(p->x[0]),
                         PX(q->z[0]), PY(q->x[0]), COL_GLASS, 255);
            draw_line(r, PX(p->z[p->n - 1]), PY(p->x[p->n - 1]),
                         PX(q->z[q->n - 1]), PY(q->x[q->n - 1]), COL_GLASS, 255);
        }
    }

    /* ---- the iris, drawn as the two blades it is ---- */
    for (int i = 0; i < lp->nprofiles; ++i) {
        const LpProfile *p = &lp->profile[i];
        if (!p->is_stop) continue;
        int px = PX(lp->stop_z);
        int inner = (int)(lp->stop_semi_ap * s);
        int outer = (int)(lp->x_max * s);
        draw_line(r, px, PY(0.0) - outer, px, PY(0.0) - inner, COL_STOP, 255);
        draw_line(r, px, PY(0.0) + inner, px, PY(0.0) + outer, COL_STOP, 255);
        draw_text_mid(r, px, PY(0.0) - outer - FONT_H - 4, 1, "STOP", COL_STOP, 200);
    }

    /* ---- the film, and the paraxial focus it is compared against ---- */
    {
        int px = PX(lp->film_z);
        int half = (int)(lp->x_max * 0.55 * s);
        draw_line(r, px, PY(0.0) - half, px, PY(0.0) + half, COL_FILM, 255);
        draw_text_mid(r, px, PY(0.0) - half - FONT_H - 4, 1, "FILM", COL_FILM, 220);
    }
    {
        int px = PX(lp->paraxial_focus_z);
        int half = (int)(lp->x_max * 0.35 * s);
        draw_dashed(r, px, PY(0.0) - half, px, PY(0.0) + half, COL_MUTED, 200);
    }

    /* ---- where each ray ACTUALLY crossed the axis ---- */
    if (show_rays && show_spot) {
        for (int i = 0; i < lp->nrays; ++i) {
            const LpRay *ray = &lp->ray[i];
            if (ray->blocked || !isfinite(ray->cross_z)) continue;
            /* cross_z is measured from the REAR vertex, everything drawn here
             * is measured from the FRONT one. One addition, stated once. */
            int px = PX(lp->rear_z + ray->cross_z);
            int py = PY(0.0);
            Col c = draw_lambda_col(ray->lambda_nm);
            draw_line(r, px, py - 5, px, py + 5, c, 200);
        }
    }

    #undef PX
    #undef PY
}

/* ---- the settings panel ---- */

#define ROW_H       (FONT_H + 8)
#define PANEL_PAD   10
#define VALUE_X     118

/* One row's y, from the panel's top. Shared by the drawing and the hit test so
 * a click always lands on the row the eye is on. */
static int row_y(SDL_Rect panel, int i) {
    return panel.y + PANEL_PAD + i * ROW_H;
}

int draw_inspector_hit(const Field *f, int n, SDL_Rect panel, int x, int y) {
    if (x < panel.x || x >= panel.x + panel.w) return -1;
    for (int i = 0; i < n; ++i) {
        if (f[i].heading || f[i].readonly) continue;
        int ry = row_y(panel, i);
        if (y >= ry - 2 && y < ry + ROW_H - 2) return i;
    }
    return -1;
}

void draw_inspector(SDL_Renderer *r, const Field *f, int n, SDL_Rect panel,
                    int sel, int hover, const char *typing) {
    draw_rect_fill(r, panel.x, panel.y, panel.w, panel.h, COL_PANEL, 255);
    draw_line(r, panel.x, panel.y, panel.x, panel.y + panel.h, COL_RULE, 255);

    char buf[64];
    for (int i = 0; i < n; ++i) {
        const Field *fd = &f[i];
        int y = row_y(panel, i);
        if (y + ROW_H > panel.y + panel.h) break;

        if (fd->heading) {
            /* A rule plus a brighter label: sections have to be findable at a
             * glance in a list this long. */
            draw_line(r, panel.x + PANEL_PAD, y + FONT_H / 2 + 1,
                      panel.x + panel.w - PANEL_PAD, y + FONT_H / 2 + 1,
                      COL_RULE, 255);
            int lw = font_text_width(fd->label, 1);
            draw_rect_fill(r, panel.x + PANEL_PAD, y, lw + 8, FONT_H, COL_PANEL, 255);
            draw_text(r, panel.x + PANEL_PAD + 4, y, 1, fd->label, COL_HEAD, 255);
            continue;
        }

        bool editable = !fd->readonly;
        if (i == sel && editable)
            draw_rect_fill(r, panel.x + 4, y - 2, panel.w - 8, ROW_H - 2,
                           COL_FACE_DOWN, 255);
        else if (i == hover && editable)
            draw_rect_fill(r, panel.x + 4, y - 2, panel.w - 8, ROW_H - 2,
                           COL_FACE, 255);

        /* A derived row is dimmer AND has no highlight, so "you can change
         * this" is never signalled by colour alone. */
        draw_text(r, panel.x + PANEL_PAD, y, 1, fd->label,
                  editable ? COL_MUTED : COL_DIM, 255);

        if (i == sel && typing) {
            snprintf(buf, sizeof buf, "%s_", typing);
            draw_text(r, panel.x + VALUE_X, y, 1, buf, COL_ACCENT, 255);
        } else {
            os_inspect_format(fd, buf, sizeof buf);
            draw_text(r, panel.x + VALUE_X, y, 1, buf,
                      editable ? COL_INK : COL_MUTED, 255);
        }
    }
}

/* ---- the scene view ---- */

const Col COL_S3_GRID    = {  52,  55,  62 };
const Col COL_S3_OBJECT  = { 150, 155, 168 };
const Col COL_S3_SUBJECT = { 255, 225,  70 };   /* the accent: in focus */
const Col COL_S3_DOF     = { 120, 200, 160 };
const Col COL_S3_LIGHT   = { 250, 205, 120 };   /* a lamp: warm, not the accent */
const Col COL_S3_SKY     = { 130, 180, 215 };   /* the dome: cool and open, so a
                                                 * placed lamp still reads as
                                                 * the warm, directional one */
const Col COL_S3_SEL     = { 120, 220, 255 };   /* selected: cold, so it cannot
                                                 * be mistaken for "in focus" */

static Col s3_colour(S3Kind k, Uint8 *alpha) {
    switch (k) {
        case S3_GRID:    *alpha = 150; return COL_S3_GRID;
        case S3_AXIS:    *alpha = 180; return COL_AXIS;
        case S3_CAMERA:  *alpha = 255; return COL_GLASS;
        case S3_FRUSTUM: *alpha = 110; return COL_MUTED;
        case S3_FOCUS:   *alpha = 230; return COL_S3_SUBJECT;
        case S3_DOF:     *alpha = 190; return COL_S3_DOF;
        case S3_SUBJECT: *alpha = 255; return COL_S3_SUBJECT;
        case S3_LIGHT:   *alpha = 235; return COL_S3_LIGHT;
        /* Dimmer than anything it surrounds. The dome is a big shape and it
         * is not the subject -- drawn at lamp brightness it would be the
         * loudest thing in a view whose point is where the objects are. */
        case S3_SKY:     *alpha = 120; return COL_S3_SKY;
        case S3_SELECTED:*alpha = 255; return COL_S3_SEL;
        default:         *alpha = 220; return COL_S3_OBJECT;
    }
}

void draw_scene3d(SDL_Renderer *r, const Scene3D *s, SDL_Rect canvas) {
    /* Drawn in kind order rather than in build order, so the ground never
     * paints over a subject and the focus planes always read on top of the
     * frustum they sit inside. Painter's algorithm by MEANING, which is what a
     * wireframe wants -- there are no surfaces to occlude anything. */
    static const S3Kind ORDER[] = {
        S3_SKY, S3_GRID, S3_FRUSTUM, S3_AXIS, S3_DOF, S3_FOCUS,
        S3_OBJECT, S3_SUBJECT, S3_LIGHT, S3_CAMERA, S3_SELECTED
    };

    for (int oi = 0; oi < (int)(sizeof ORDER / sizeof ORDER[0]); ++oi) {
        S3Kind want = ORDER[oi];
        Uint8 a;
        Col c = s3_colour(want, &a);
        for (int i = 0; i < s->nseg; ++i) {
            if (s->seg[i].kind != want) continue;
            ls_real x0, y0, x1, y1;
            /* Both ends must be in front of the eye. Clipping the segment
             * instead would be more correct and is not worth it here: the
             * orbit camera cannot get inside the geometry. */
            if (!s3_project(s, s->seg[i].a, canvas.w, canvas.h, &x0, &y0)) continue;
            if (!s3_project(s, s->seg[i].b, canvas.w, canvas.h, &x1, &y1)) continue;
            draw_line(r, canvas.x + (int)x0, canvas.y + (int)y0,
                         canvas.x + (int)x1, canvas.y + (int)y1, c, a);
        }
    }

    for (int i = 0; i < s->nlabel; ++i) {
        ls_real x, y;
        if (!s3_project(s, s->label[i].at, canvas.w, canvas.h, &x, &y)) continue;
        Uint8 a;
        Col c = s3_colour(s->label[i].kind, &a);
        draw_text(r, canvas.x + (int)x + 4, canvas.y + (int)y - FONT_H / 2, 1,
                  s->label[i].text, c, 255);
    }

    /* What the slab actually is, in numbers, for anyone who wants the value
     * rather than the picture. */
    char buf[96];
    if (s->near_m > 0.0) {
        if (isfinite(s->far_m))
            snprintf(buf, sizeof buf, "SHARP %.2fM TO %.2fM   HYPERFOCAL %.1fM",
                     (double)s->near_m, (double)s->far_m, (double)s->hyperfocal_m);
        else
            snprintf(buf, sizeof buf, "SHARP %.2fM TO INFINITY   HYPERFOCAL %.1fM",
                     (double)s->near_m, (double)s->hyperfocal_m);
        draw_text(r, canvas.x + 12, canvas.y + 10, 1, buf, COL_S3_DOF, 255);
    }
    draw_text(r, canvas.x + 12, canvas.y + 10 + FONT_H + 4, 1,
              "DRAG TO ORBIT   WHEEL TO ZOOM", COL_DIM, 255);
}
