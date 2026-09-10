/* draw.h — SDL drawing: palette, primitives, toolbar, tooltip, help.
 *
 * The palette is Linkage-Design's, taken from its render_toolbar() so the two
 * tools read as one family: a graphite ground, button faces that lift on
 * hover, and exactly ONE accent -- the selection yellow -- so "this is on" is
 * never said two different ways.
 *
 * This file and main.c are the only two that may include SDL; check-sdl-purity
 * in the Makefile enforces it, which is what lets ui.c, lensplot.c, font.c and
 * status.c be exercised with no window.
 */
#ifndef OPTICSIM_VIEWER_DRAW_H
#define OPTICSIM_VIEWER_DRAW_H

#include <SDL2/SDL.h>
#include "ui.h"
#include "status.h"
#include "lensplot.h"
#include "inspect.h"
#include "scene3d.h"

typedef struct { Uint8 r, g, b; } Col;

extern const Col COL_BG, COL_PANEL, COL_RULE;
extern const Col COL_FACE, COL_FACE_HOT, COL_FACE_DOWN, COL_FACE_OFF;
extern const Col COL_EDGE, COL_EDGE_OFF;
extern const Col COL_INK, COL_HEAD, COL_MUTED, COL_DIM;
extern const Col COL_ACCENT, COL_ACCENT_DIM;
extern const Col COL_WARN, COL_ERR;
/* The optics: glass, the iris, the film plane, and the three wavelengths. */
extern const Col COL_GLASS, COL_GLASS_FILL, COL_STOP, COL_FILM, COL_AXIS;
extern const Col COL_RAY, COL_RAY_BLOCKED, COL_RAY_F, COL_RAY_D, COL_RAY_C;
/* The scene view: subjects, the one in focus, the sharp slab, the frustum. */
extern const Col COL_S3_GRID, COL_S3_OBJECT, COL_S3_SUBJECT, COL_S3_DOF;
extern const Col COL_S3_LIGHT, COL_S3_SEL, COL_S3_SKY;

Col draw_level_col(StatusLevel level);
/* The colour a traced ray is drawn in, from its wavelength. */
Col draw_lambda_col(ls_real lambda_nm);

void draw_rect_fill(SDL_Renderer *r, int x, int y, int w, int h, Col c, Uint8 a);
void draw_rect_line(SDL_Renderer *r, int x, int y, int w, int h, Col c, Uint8 a);
void draw_line(SDL_Renderer *r, int x0, int y0, int x1, int y1, Col c, Uint8 a);
void draw_dashed(SDL_Renderer *r, int x0, int y0, int x1, int y1, Col c, Uint8 a);
void draw_text(SDL_Renderer *r, int x, int y, int scale, const char *s, Col c, Uint8 a);
void draw_text_right(SDL_Renderer *r, int rx, int y, int scale, const char *s, Col c, Uint8 a);
void draw_text_mid(SDL_Renderer *r, int mx, int y, int scale, const char *s, Col c, Uint8 a);

void draw_toolbar(SDL_Renderer *r, const Toolbar *t, int strip_h);
void draw_tooltip(SDL_Renderer *r, UiRect anchor, const char *text, int win_w, int win_h);
void draw_status(SDL_Renderer *r, const StatusLog *log, SDL_Rect canvas, Uint32 now);
void draw_help(SDL_Renderer *r, const Toolbar *t, SDL_Rect canvas);

/* ---- the settings panel ----
 *
 * Draws the field list os_inspect_fields() produced. `sel` is the row being
 * edited (-1 for none), `typing` the digits entered so far (NULL when not
 * typing). Row geometry lives here and is shared with draw_inspector_hit, so
 * what is drawn and what is clickable cannot drift apart. */
void draw_inspector(SDL_Renderer *r, const Field *f, int n, SDL_Rect panel,
                    int sel, int hover, const char *typing);
int  draw_inspector_hit(const Field *f, int n, SDL_Rect panel, int x, int y);

/* The camera and its subjects in space. Segments come from scene3d.c already
 * in world coordinates; this projects and rasterises them and nothing else. */
void draw_scene3d(SDL_Renderer *r, const Scene3D *s, SDL_Rect canvas);

/* The cross-section itself. `canvas` is the region it may use; the plot is
 * fitted into it from lp's own millimetre extents, so this function knows no
 * optics at all. */
void draw_lensplot(SDL_Renderer *r, const LensPlot *lp, SDL_Rect canvas,
                   bool show_rays, bool show_spot, bool show_grid);

#endif /* OPTICSIM_VIEWER_DRAW_H */
