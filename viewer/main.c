/* main.c — the viewer: three views, one settings model, one render thread.
 *
 * WHAT THIS FILE OWNS
 *   The window, the event loop, and the rule that every setting lives in one
 *   OsSettings struct which the render thread is handed a copy of. Changing
 *   anything goes through os_inspect_set(), whether it came from a drag, a
 *   typed number or a toolbar button -- so no control can reach a state
 *   another control cannot.
 *
 * THE RENDER THREAD
 *   The image view accumulates progressively: one pass per loop, added into
 *   the same film, so leaving it alone converges and touching anything starts
 *   over. The worker is PARKED before any mutation -- pause, wait for the
 *   acknowledgement, mutate, resume -- rather than locked per sample. Nothing
 *   takes a lock per ray.
 *
 *   Only the settings that change the PHOTOGRAPH restart it. Exposure does
 *   not: it is a view gain applied when the film is tone-mapped, and throwing
 *   away a half-converged render to brighten it would be maddening.
 *
 * THE THREE VIEWS
 *   SCENE shows where everything IS -- the camera, what it can see, the
 *   plane it is focused on and the slab either side of that which is still
 *   sharp. LENS shows how the glass bends light. IMAGE shows what came out.
 *   They read the same lens and the same stage, so they cannot disagree.
 *
 * Only this file and draw.c may include SDL; check-sdl-purity enforces it,
 * which is what lets ui.c, inspect.c, lensplot.c, scene3d.c, font.c and
 * status.c be tested with no window.
 */
#include <SDL2/SDL.h>

#include "viewer.h"
#include "draw.h"
#include "font.h"
#include "ui.h"
#include "inspect.h"
#include "lensplot.h"
#include "scene3d.h"
#include "status.h"

#include "opticsim/camera.h"
#include "opticsim/render.h"
#include "opticsim/scenedesc.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN_W 1280
#define WIN_H  800
#define PANEL_W 250
#define TOOLTIP_DELAY_MS 450
#define RENDER_SEED 0x853C49E6748FEA9Bull

/* One third of a stop: what a real aperture ring does, and fine enough to
 * watch the marginal rays leave one at a time. */
#define STOP_THIRD 1.1224620483093730

/* ---- the render side ---------------------------------------------------- */

typedef struct {
    OsSettings   set;          /* the settings the worker is rendering */
    OsCamera     cam;
    OsStage      st;
    bool         built;
    char         why[256];

    Film         film;
    int          W, H;
    unsigned char *rgb;        /* tone-mapped, W*H*3 */
    SDL_mutex   *lock;         /* guards rgb only */

    SDL_atomic_t quit, pause_req, paused_ack, restart, passes;
    SDL_Thread  *thread;
} Renderer;

static void renderer_teardown(Renderer *R) {
    if (R->built) {
        os_camera_free(&R->cam);
        os_stage_free(&R->st);
        ls_film_free(&R->film);
        R->built = false;
    }
    free(R->rgb);
    R->rgb = NULL;
    R->W = R->H = 0;
}

/* Rebuild the scene, the camera and the film from `s`. Only ever called with
 * the worker parked. */
static bool renderer_build(Renderer *R, const OsSettings *s) {
    renderer_teardown(R);

    /* Built from the ARRANGEMENT, not from the preset id. That one line is
     * what makes the scene editable at all: building from the id regenerated
     * it on every settings change, so an edit lasted until the next time the
     * aperture moved. */
    if (!os_scenedesc_build(&s->scene, &R->st)) {
        snprintf(R->why, sizeof R->why, "could not build the scene");
        return false;
    }
    int w = s->res_w, h = os_settings_res_h(s);
    if (!os_camera_build(&R->cam, (OsPrescriptionId)s->lens, s->focal_mm,
                         s->fno, s->sensor_w_mm, w, h, R->why, sizeof R->why)) {
        os_stage_free(&R->st);
        return false;
    }
    R->cam.lens.blades          = s->blades;
    R->cam.lens.blade_curvature = s->curvature;
    R->cam.lens.blade_rot_rad   = s->rot_deg * LS_PI / 180.0;
    os_lens_set_fnumber(&R->cam.lens, s->fno);
    os_lens_focus(&R->cam.lens, s->focus_m);
    os_camera_refresh(&R->cam);
    os_camera_look_at(&R->cam, R->st.cam_eye, R->st.cam_target, v3(0, 1, 0));

    if (!ls_film_init(&R->film, w, h)) {
        snprintf(R->why, sizeof R->why, "out of memory for a %dx%d film", w, h);
        os_camera_free(&R->cam);
        os_stage_free(&R->st);
        return false;
    }
    R->rgb = calloc((size_t)w * (size_t)h * 3, 1);
    if (!R->rgb) {
        snprintf(R->why, sizeof R->why, "out of memory for the display buffer");
        ls_film_free(&R->film);
        os_camera_free(&R->cam);
        os_stage_free(&R->st);
        return false;
    }
    R->W = w; R->H = h;
    R->built = true;
    R->why[0] = '\0';
    return true;
}

/* Film to bytes. The exposure here is a VIEW gain and nothing more -- it does
 * not touch the film, which stays in physical units. When the sensor model
 * lands, brightness comes from the exposure triangle and this becomes a
 * viewing convenience rather than the only control. */
static void tonemap(Renderer *R) {
    if (!R->built || !R->rgb) return;
    ls_real e = R->set.exposure;
    SDL_LockMutex(R->lock);
    for (int y = 0; y < R->H; ++y)
        for (int x = 0; x < R->W; ++x) {
            Spectrum s = ls_film_mean(&R->film, x, y);
            RGB c = ls_xyz_to_linear_srgb(ls_spectrum_to_xyz(&s));
            c.r *= e; c.g *= e; c.b *= e;
            c = ls_rgb_gamma_encode(c);
            size_t o = ((size_t)y * (size_t)R->W + (size_t)x) * 3;
            R->rgb[o + 0] = (unsigned char)(ls_clamp(c.r, 0.0, 1.0) * 255.0 + 0.5);
            R->rgb[o + 1] = (unsigned char)(ls_clamp(c.g, 0.0, 1.0) * 255.0 + 0.5);
            R->rgb[o + 2] = (unsigned char)(ls_clamp(c.b, 0.0, 1.0) * 255.0 + 0.5);
        }
    SDL_UnlockMutex(R->lock);
}

static int render_worker(void *data) {
    Renderer *R = data;
    while (!SDL_AtomicGet(&R->quit)) {
        if (SDL_AtomicGet(&R->pause_req)) {
            SDL_AtomicSet(&R->paused_ack, 1);
            SDL_Delay(2);
            continue;
        }
        SDL_AtomicSet(&R->paused_ack, 0);

        if (!R->built) { SDL_Delay(10); continue; }

        if (SDL_AtomicSet(&R->restart, 0)) {
            size_t np = (size_t)R->W * (size_t)R->H;
            memset(R->film.pix, 0, np * sizeof *R->film.pix);
            memset(R->film.n,   0, np * sizeof *R->film.n);
            SDL_AtomicSet(&R->passes, 0);
        }

        int pass = SDL_AtomicGet(&R->passes);
        OsRenderOpts opt = { R->set.spp, R->set.depth, 0, RENDER_SEED };
        os_render_pass(&R->film, &R->cam, &R->st, &opt, pass);
        SDL_AtomicAdd(&R->passes, 1);
        tonemap(R);
    }
    return 0;
}

static void renderer_park(Renderer *R) {
    if (!R->thread) return;
    SDL_AtomicSet(&R->pause_req, 1);
    /* Spin until the worker says it has stopped. Mutating the scene under a
     * running trace is a use-after-free waiting to happen, and it would only
     * show up on a machine with the right number of cores. */
    while (!SDL_AtomicGet(&R->paused_ack)) SDL_Delay(1);
}

static void renderer_resume(Renderer *R) {
    SDL_AtomicSet(&R->pause_req, 0);
}

/* ---- the app ------------------------------------------------------------ */

typedef struct {
    SDL_Window   *win;
    SDL_Renderer *ren;
    SDL_Texture  *tex;
    int           tex_w, tex_h;
    bool          running;

    OsSettings    set;
    Renderer      R;

    LensPlot      plot;
    bool          plot_stale;
    Scene3D       s3;
    bool          s3_stale;

    Toolbar       toolbar;
    StatusLog     status;
    Uint32        hover_since;

    Field         fields[OS_INSPECT_MAX];
    int           nfields;
    int           sel_row, hover_row;
    bool          scrubbing;
    int           scrub_x;
    /* Dragging a subject in the scene view. `grab` is the offset from the
     * object's centre to where the cursor actually landed on the drag plane,
     * so the thing does not jump to centre itself under the pointer on the
     * first motion event. */
    bool          moving;
    vec3          grab;
    char          typing[24];
    bool          is_typing;

    ls_real       zoom, pan_x, pan_y;
    bool          dragging;
    int           drag_x, drag_y;
} App;

static void say(App *a, StatusLevel lv, const char *fmt, ...) {
    char buf[STATUS_MAX_TEXT];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    status_push(&a->status, lv, SDL_GetTicks(), buf);
}

/* THE one place a settings change takes effect.
 *
 * Everything -- a drag, a typed number, a toolbar button, a hotkey -- ends up
 * here, so there is a single answer to "what has to happen when a setting
 * moves". Adding a setting means adding it to os_settings_image_differs() and
 * nothing else. */
static void apply(App *a) {
    bool need_rebuild = os_settings_image_differs(&a->set, &a->R.set);

    renderer_park(&a->R);
    a->R.set = a->set;
    if (need_rebuild) {
        if (renderer_build(&a->R, &a->set)) {
            status_clear_sticky(&a->status);
            SDL_AtomicSet(&a->R.restart, 1);
        } else {
            status_set_sticky(&a->status, STATUS_ERROR, a->R.why);
        }
    } else {
        /* Exposure moved but the photograph did not: re-map the film that is
         * already there rather than throwing away its convergence. */
        tonemap(&a->R);
    }

    /* Echo any aperture clamp back into the settings, so the panel never shows
     * an f-number the lens is not actually using. */
    if (a->R.built) {
        a->set.fno   = a->R.cam.lens.f_number;
        a->R.set.fno = a->set.fno;
    }
    renderer_resume(&a->R);
    a->plot_stale = true;
    a->s3_stale   = true;
}

static UiState ui_state(App *a) {
    UiState s;
    memset(&s, 0, sizeof s);
    s.view         = a->set.view;
    s.rendering    = a->R.built && SDL_AtomicGet(&a->R.passes) > 0;
    s.showing_rays = a->set.show_rays;
    s.showing_spot = a->set.show_spot;
    s.showing_grid = a->set.show_grid;
    s.chromatic    = a->set.chromatic;
    s.blades       = a->set.blades;
    s.lens_index   = a->set.lens;
    s.lens_count   = OS_LENS_COUNT;
    s.focal_mm     = a->set.focal_mm; s.focal_min_mm = 12.0;  s.focal_max_mm = 400.0;
    s.fno          = a->set.fno;      s.fno_min      = 1.0;   s.fno_max      = 45.0;
    s.focus_m      = a->set.focus_m;  s.focus_min_m  = 0.15;  s.focus_max_m  = 1000.0;

    s.has_selection   = (a->set.sel_obj >= 0 || a->set.sel_light >= 0);
    s.ambient         = (a->set.scene.light_mode == OS_LIGHT_AMBIENT);
    s.room_for_object = a->set.scene.nobj < OS_MAX_OBJECTS;
    s.room_for_light  = a->set.scene.nlit < OS_MAX_LIGHTS;
    s.scene_items     = os_scenedesc_count_objects(&a->set.scene)
                      + os_scenedesc_count_lights(&a->set.scene);
    return s;
}

static void dispatch(App *a, UiAction act) {
    switch (act) {
        case UI_VIEW_SCENE: a->set.view = OS_VIEW_SCENE; return;
        case UI_VIEW_LENS:  a->set.view = OS_VIEW_LENS;  return;
        case UI_VIEW_IMAGE: a->set.view = OS_VIEW_IMAGE; return;

        case UI_LENS_NEXT:
            os_inspect_set(&a->set, FLD_LENS, (a->set.lens + 1) % OS_LENS_COUNT);
            apply(a);
            say(a, STATUS_INFO, "MOUNTED %s",
                a->R.built ? a->R.cam.lens.name : "?");
            return;

        case UI_OPEN_UP:
            os_inspect_set(&a->set, FLD_FNO, a->set.fno / STOP_THIRD);
            apply(a);
            return;
        case UI_STOP_DOWN:
            os_inspect_set(&a->set, FLD_FNO, a->set.fno * STOP_THIRD);
            apply(a);
            return;
        case UI_FOCUS_NEAR:
            os_inspect_set(&a->set, FLD_FOCUS, a->set.focus_m / 1.25);
            apply(a);
            return;
        case UI_FOCUS_FAR:
            os_inspect_set(&a->set, FLD_FOCUS, a->set.focus_m * 1.25);
            apply(a);
            return;

        case UI_BLADES: {
            int b = a->set.blades == 0 ? 3
                  : (a->set.blades >= 14 ? 0 : a->set.blades + 1);
            os_inspect_set(&a->set, FLD_BLADES, b);
            apply(a);
            if (a->set.blades == 0) say(a, STATUS_INFO, "IRIS: PERFECT CIRCLE");
            else say(a, STATUS_INFO, "IRIS: %d BLADES, SAME AREA", a->set.blades);
            return;
        }

        case UI_RAYS: a->set.show_rays = !a->set.show_rays; a->plot_stale = true; return;
        case UI_SPOT: a->set.show_spot = !a->set.show_spot; return;
        case UI_GRID: a->set.show_grid = !a->set.show_grid; return;
        case UI_COLOUR:
            a->set.chromatic = !a->set.chromatic;
            a->plot_stale = true;
            say(a, STATUS_INFO, a->set.chromatic ? "TRACING F, D AND C LINES"
                                                 : "TRACING THE D LINE ONLY");
            return;

        case UI_SAVE: {
            if (!a->R.built) return;
            renderer_park(&a->R);
            bool ok = ls_film_write_ppm(&a->R.film, "out/viewer.ppm",
                                        a->set.exposure);
            renderer_resume(&a->R);
            if (ok) say(a, STATUS_INFO, "WROTE OUT/VIEWER.PPM");
            else    say(a, STATUS_ERROR, "CANNOT WRITE OUT/VIEWER.PPM -- DOES OUT/ EXIST?");
            return;
        }

        case UI_ADD_OBJECT: {
            int id = os_scenedesc_add_object(&a->set.scene, OS_OBJ_SPHERE);
            if (id < 0) { say(a, STATUS_WARN, "NO ROOM FOR ANOTHER OBJECT"); return; }
            /* Placed on the focus plane, so a new object arrives somewhere you
             * can see it rather than at the origin inside the camera. */
            a->set.scene.obj[id].centre = v3(0.0, 0.0, -a->set.focus_m);
            a->set.scene.obj[id].radius = 0.03 * a->set.focus_m;
            os_scenedesc_clamp_object(&a->set.scene.obj[id]);
            a->set.sel_obj = id;
            a->set.sel_light = -1;
            apply(a);
            say(a, STATUS_INFO, "ADDED %s AT %.2fM",
                a->set.scene.obj[id].name, (double)a->set.focus_m);
            return;
        }

        case UI_ADD_LIGHT: {
            int id = os_scenedesc_add_light(&a->set.scene, OS_LIGHT_SPHERE);
            if (id < 0) { say(a, STATUS_WARN, "NO ROOM FOR ANOTHER LAMP"); return; }
            a->set.sel_light = id;
            a->set.sel_obj = -1;
            apply(a);
            say(a, STATUS_INFO, "ADDED %s, %.0f LM",
                a->set.scene.lit[id].name, (double)a->set.scene.lit[id].flux_lm);
            return;
        }

        case UI_LIGHT_MODE: {
            OsSceneDesc *d = &a->set.scene;
            d->light_mode = (d->light_mode == OS_LIGHT_AMBIENT)
                          ? OS_LIGHT_LAMPS : OS_LIGHT_AMBIENT;
            apply(a);
            if (d->light_mode == OS_LIGHT_AMBIENT)
                say(a, STATUS_INFO, "AMBIENT: %.0f LX DOME, LAMPS OFF",
                    (double)d->ambient_lux);
            else
                say(a, STATUS_INFO, "LAMPS: %d PLACED",
                    os_scenedesc_count_lights(d));
            return;
        }

        case UI_DELETE: {
            bool did = false;
            if (a->set.sel_obj >= 0)
                did = os_scenedesc_delete_object(&a->set.scene, a->set.sel_obj);
            else if (a->set.sel_light >= 0)
                did = os_scenedesc_delete_light(&a->set.scene, a->set.sel_light);
            if (!did) return;
            a->set.sel_obj = a->set.sel_light = -1;
            apply(a);
            /* Every other id is untouched -- that is the point of tombstoning
             * rather than compacting -- so nothing else the panel could have
             * been editing has quietly become a different thing. */
            say(a, STATUS_INFO, "DELETED; EVERY OTHER ID IS UNCHANGED");
            return;
        }

        case UI_SELECT_NEXT: {
            /* Objects, then lamps, then back to nothing. Steps over the
             * tombstones without the caller knowing they exist. */
            if (a->set.sel_light >= 0) {
                int n = os_scenedesc_next_light(&a->set.scene, a->set.sel_light + 1);
                a->set.sel_light = n;
                if (n < 0) a->set.sel_obj = os_scenedesc_next_object(&a->set.scene, 0);
            } else {
                int n = os_scenedesc_next_object(&a->set.scene,
                                                 a->set.sel_obj < 0 ? 0 : a->set.sel_obj + 1);
                if (n >= 0) { a->set.sel_obj = n; }
                else { a->set.sel_obj = -1;
                       a->set.sel_light = os_scenedesc_next_light(&a->set.scene, 0); }
            }
            a->s3_stale = true;
            return;
        }

        case UI_RESET: {
            int view = a->set.view;
            os_settings_default(&a->set);
            a->set.view = view;
            a->zoom = 1.0; a->pan_x = a->pan_y = 0.0;
            apply(a);
            say(a, STATUS_INFO, "SETTINGS RESET");
            return;
        }

        default: return;
    }
}

/* Clicked or typed, every command funnels through here so a refused one
 * explains itself in its own words instead of doing nothing. */
static void try_action(App *a, UiAction act, bool *helping) {
    if (act == UI_NONE) return;
    if (act == UI_HELP) { *helping = !*helping; return; }
    ui_apply_state(&a->toolbar, ui_state(a));
    if (ui_action_enabled(&a->toolbar, act)) { dispatch(a, act); return; }
    say(a, STATUS_WARN, "%s: %s", ui_action_label(&a->toolbar, act),
        ui_action_tip(&a->toolbar, act));
}

/* ---- drawing ---- */

static void draw_image_view(App *a, SDL_Rect canvas) {
    Renderer *R = &a->R;
    if (!R->built) {
        draw_text_mid(a->ren, canvas.x + canvas.w / 2,
                      canvas.y + canvas.h / 2 - FONT_H, 1,
                      "NO IMAGE: THE LENS WOULD NOT BUILD", COL_ERR, 255);
        return;
    }

    if (!a->tex || a->tex_w != R->W || a->tex_h != R->H) {
        if (a->tex) SDL_DestroyTexture(a->tex);
        a->tex = SDL_CreateTexture(a->ren, SDL_PIXELFORMAT_RGB24,
                                   SDL_TEXTUREACCESS_STREAMING, R->W, R->H);
        a->tex_w = R->W; a->tex_h = R->H;
    }
    if (!a->tex) return;

    SDL_LockMutex(R->lock);
    SDL_UpdateTexture(a->tex, NULL, R->rgb, R->W * 3);
    SDL_UnlockMutex(R->lock);

    /* Fit, preserving aspect: a photograph shown at the wrong shape is worse
     * than one shown small. */
    double sx = (double)(canvas.w - 24) / (double)R->W;
    double sy = (double)(canvas.h - 24) / (double)R->H;
    double s = (sx < sy ? sx : sy) * (double)a->zoom;
    int dw = (int)((double)R->W * s), dh = (int)((double)R->H * s);
    SDL_Rect dst = { canvas.x + (canvas.w - dw) / 2 + (int)a->pan_x,
                     canvas.y + (canvas.h - dh) / 2 + (int)a->pan_y, dw, dh };
    SDL_RenderCopy(a->ren, a->tex, NULL, &dst);
    draw_rect_line(a->ren, dst.x - 1, dst.y - 1, dst.w + 2, dst.h + 2,
                   COL_RULE, 255);

    int passes = SDL_AtomicGet(&R->passes);
    char buf[96];
    snprintf(buf, sizeof buf, "%d X %d   %d SPP   PASS %d",
             R->W, R->H, R->set.spp * passes, passes);
    draw_text(a->ren, canvas.x + 12, canvas.y + 10, 1, buf,
              passes ? COL_MUTED : COL_DIM, 255);
}

static void draw_frame(App *a, bool helping) {
    ui_apply_state(&a->toolbar, ui_state(a));

    SDL_SetRenderDrawColor(a->ren, COL_BG.r, COL_BG.g, COL_BG.b, 255);
    SDL_RenderClear(a->ren);

    SDL_Rect canvas = { UI_TOOLBAR_W, 0,
                        WIN_W - UI_TOOLBAR_W - PANEL_W, WIN_H };
    SDL_RenderSetClipRect(a->ren, &canvas);

    if (a->set.view == OS_VIEW_SCENE) {
        if (a->R.built) {
            /* nseg == 0 means never built: s3_build always emits at least
             * the grid and the camera. Rebuilding on that as well as on the
             * flag means a missed `s3_stale = true` costs one frame, not an
             * empty view. */
            if (a->s3_stale || a->s3.nseg == 0) {
                s3_build(&a->s3, &a->set.scene, &a->R.cam.lens,
                         a->R.cam.sensor_w_mm, a->R.cam.sensor_h_mm,
                         a->set.coc_limit_mm, a->set.sel_obj, a->set.sel_light);
                a->s3_stale = false;
            }
            draw_scene3d(a->ren, &a->s3, canvas);
        }
    } else if (a->set.view == OS_VIEW_IMAGE) {
        draw_image_view(a, canvas);
    } else if (a->R.built) {
        if (a->plot_stale) {
            lp_build(&a->plot, &a->R.cam.lens, 15, a->set.chromatic,
                     a->set.focus_m);
            a->plot_stale = false;
        }
        SDL_Rect view = {
            (int)((double)canvas.x + (double)a->pan_x
                  - (double)canvas.w * ((double)a->zoom - 1.0) * 0.5),
            (int)((double)canvas.y + (double)a->pan_y
                  - (double)canvas.h * ((double)a->zoom - 1.0) * 0.5),
            (int)((double)canvas.w * (double)a->zoom),
            (int)((double)canvas.h * (double)a->zoom)
        };
        draw_lensplot(a->ren, &a->plot, view, a->set.show_rays,
                      a->set.show_spot, a->set.show_grid);
    }

    draw_status(a->ren, &a->status, canvas, SDL_GetTicks());
    if (helping) draw_help(a->ren, &a->toolbar, canvas);
    SDL_RenderSetClipRect(a->ren, NULL);

    SDL_Rect panel = { WIN_W - PANEL_W, 0, PANEL_W, WIN_H };
    uint64_t done = a->R.built
        ? (uint64_t)a->R.set.spp * (uint64_t)SDL_AtomicGet(&a->R.passes) : 0;
    a->nfields = os_inspect_fields(&a->set, a->R.built ? &a->R.cam.lens : NULL,
                                   done, a->fields, OS_INSPECT_MAX);
    draw_inspector(a->ren, a->fields, a->nfields, panel, a->sel_row,
                   a->hover_row, a->is_typing ? a->typing : NULL);

    draw_toolbar(a->ren, &a->toolbar, WIN_H);

    if (a->toolbar.hover >= 0 && !helping
        && SDL_GetTicks() - a->hover_since > TOOLTIP_DELAY_MS) {
        const UiButton *b = &a->toolbar.buttons[a->toolbar.hover];
        draw_tooltip(a->ren, b->rect, b->tip, WIN_W, WIN_H);
    }

    SDL_RenderPresent(a->ren);
}

static int run_capture(const char *subject, const char *dir);

/* ---- entry -------------------------------------------------------------- */

int os_viewer_main(int argc, char **argv) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (argc >= 2 && !strcmp(argv[1], "--capture")) {
        if (argc != 4) {
            fprintf(stderr, "usage: %s --capture <subject> <dir>\n"
                            "  subjects: wide singlet achromat stopped blades image scene edit lamp\n"
                            "            ambient ambientimage\n",
                    argv[0]);
            SDL_Quit();
            return 2;
        }
        int rc = run_capture(argv[2], argv[3]);
        SDL_Quit();
        return rc;
    }

    App a;
    memset(&a, 0, sizeof a);
    a.running   = true;
    a.sel_row   = -1;
    a.hover_row = -1;
    a.zoom      = 1.0;
    os_settings_default(&a.set);
    a.set.view = OS_VIEW_SCENE;
    s3_init(&a.s3);
    status_init(&a.status);

    a.win = SDL_CreateWindow("opticsim", SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H,
                             SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
                             | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!a.win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    a.ren = SDL_CreateRenderer(a.win, -1, SDL_RENDERER_ACCELERATED);
    if (!a.ren) a.ren = SDL_CreateRenderer(a.win, -1, SDL_RENDERER_SOFTWARE);
    if (!a.ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderDrawBlendMode(a.ren, SDL_BLENDMODE_BLEND);
    SDL_RenderSetLogicalSize(a.ren, WIN_W, WIN_H);

    ui_init(&a.toolbar, WIN_H);
    a.toolbar.hover = -1;
    a.toolbar.pressed = -1;

    a.R.lock = SDL_CreateMutex();
    a.R.set = a.set;
    if (!renderer_build(&a.R, &a.set))
        status_set_sticky(&a.status, STATUS_ERROR, a.R.why);
    SDL_AtomicSet(&a.R.restart, 1);
    a.R.thread = SDL_CreateThread(render_worker, "render", &a.R);
    /* Both views start unbuilt. Forgetting the scene one here left the first
     * frame empty until something -- reset, or any settings change -- ran
     * apply() and set the flag; the capture path below sets both, which is
     * exactly why every checked-in screenshot looked right. */
    a.plot_stale = true;
    a.s3_stale   = true;

    say(&a, STATUS_INFO, "1 SCENE   2 LENS   3 IMAGE   ? FOR KEYS");

    bool helping = false;

    while (a.running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT: a.running = false; break;

                case SDL_TEXTINPUT: {
                    /* Type-to-set. Only the pieces of a number are accepted, so
                     * a stray keystroke cannot half-commit an edit. */
                    if (a.sel_row < 0 || a.sel_row >= a.nfields) break;
                    const Field *f = &a.fields[a.sel_row];
                    if (f->readonly || f->heading || f->is_enum) break;
                    char c = e.text.text[0];
                    if (!((c >= '0' && c <= '9') || c == '.' || c == '-')) break;
                    size_t n = strlen(a.typing);
                    if (n + 1 < sizeof a.typing) {
                        a.typing[n] = c;
                        a.typing[n + 1] = '\0';
                        a.is_typing = true;
                    }
                    break;
                }

                case SDL_KEYDOWN: {
                    SDL_Keycode k = e.key.keysym.sym;

                    if (a.is_typing) {
                        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                            double v = atof(a.typing);
                            if (os_inspect_set(&a.set, a.fields[a.sel_row].id, v))
                                apply(&a);
                            a.is_typing = false; a.typing[0] = '\0';
                        } else if (k == SDLK_BACKSPACE) {
                            size_t n = strlen(a.typing);
                            if (n) a.typing[n - 1] = '\0';
                            if (!a.typing[0]) a.is_typing = false;
                        } else if (k == SDLK_ESCAPE) {
                            a.is_typing = false; a.typing[0] = '\0';
                        }
                        break;
                    }

                    if (k == SDLK_ESCAPE) {
                        /* A cancel ladder: escape closes what is open before it
                         * closes the program, so quitting is never a surprise. */
                        if (helping)             helping = false;
                        else if (a.sel_row >= 0) a.sel_row = -1;
                        else                     a.running = false;
                        break;
                    }

                    /* Arrow keys nudge the selected row, which is how a value
                     * gets set exactly rather than approximately. */
                    if (a.sel_row >= 0 && a.sel_row < a.nfields
                        && (k == SDLK_UP || k == SDLK_DOWN
                         || k == SDLK_LEFT || k == SDLK_RIGHT)) {
                        const Field *f = &a.fields[a.sel_row];
                        int dir = (k == SDLK_UP || k == SDLK_RIGHT) ? 1 : -1;
                        int step = (k == SDLK_UP || k == SDLK_DOWN) ? 10 : 1;
                        double nv = f->is_enum ? f->value + dir
                                  : os_inspect_scrub(f, f->value, dir * step);
                        if (os_inspect_set(&a.set, f->id, nv)) apply(&a);
                        break;
                    }

                    if (k < 128) try_action(&a, ui_action_for_key((char)k), &helping);
                    break;
                }

                case SDL_MOUSEMOTION: {
                    float lx, ly;
                    SDL_RenderWindowToLogical(a.ren, e.motion.x, e.motion.y, &lx, &ly);
                    int mx = (int)lx, my = (int)ly;

                    int h = ui_hit(&a.toolbar, mx, my);
                    if (h != a.toolbar.hover) {
                        a.toolbar.hover = h;
                        a.hover_since = SDL_GetTicks();
                    }
                    SDL_Rect panel = { WIN_W - PANEL_W, 0, PANEL_W, WIN_H };
                    a.hover_row = draw_inspector_hit(a.fields, a.nfields, panel, mx, my);

                    if (a.moving) {
                        /* Slide on the horizontal plane through the object's
                         * own height, so a drag moves it about the floor
                         * without also raising it. Shift drags the height
                         * instead -- the one axis a ground plane cannot give. */
                        SDL_Rect cv = { UI_TOOLBAR_W, 0,
                                        WIN_W - UI_TOOLBAR_W - PANEL_W, WIN_H };
                        bool shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
                        int oid = a.set.sel_obj, lid = a.set.sel_light;
                        vec3 c = (oid >= 0) ? a.set.scene.obj[oid].centre
                               : (lid >= 0) ? a.set.scene.lit[lid].centre
                                            : v3(0,0,0);
                        bool changed = false;
                        if (shift) {
                            /* Vertical: screen dy, scaled by how far away the
                             * thing is, so the gesture feels the same at any
                             * distance. */
                            ls_real k = 0.004 * (ls_real)(a.s3.dist);
                            ls_real ny = c.y - (ls_real)(my - a.drag_y) * k;
                            changed = os_inspect_set(&a.set,
                                oid >= 0 ? FLD_O_Y : FLD_L_Y, ny);
                        } else {
                            vec3 hit;
                            if (s3_plane_hit(&a.s3, cv.w, cv.h, mx - cv.x,
                                             my - cv.y, c.y, &hit)) {
                                vec3 want = v3add(hit, a.grab);
                                /* Through os_inspect_set like every other
                                 * edit, so a drag clamps exactly as a typed
                                 * value does. */
                                bool cx = os_inspect_set(&a.set,
                                    oid >= 0 ? FLD_O_X : FLD_L_X, want.x);
                                bool cz = os_inspect_set(&a.set,
                                    oid >= 0 ? FLD_O_Z : FLD_L_Z, want.z);
                                changed = cx || cz;
                            }
                        }
                        a.drag_x = mx; a.drag_y = my;
                        if (changed) apply(&a);
                    } else if (a.scrubbing && a.sel_row >= 0 && a.sel_row < a.nfields) {
                        const Field *f = &a.fields[a.sel_row];
                        int dx = mx - a.scrub_x;
                        if (dx) {
                            double nv = os_inspect_scrub(f, f->value, dx);
                            if (os_inspect_set(&a.set, f->id, nv)) apply(&a);
                            a.scrub_x = mx;
                        }
                    } else if (a.dragging) {
                        if (a.set.view == OS_VIEW_SCENE) {
                            /* A drag ORBITS here rather than panning. Panning a
                             * perspective view of a line of objects going away
                             * from you tells you nothing; turning it tells you
                             * everything. */
                            s3_orbit(&a.s3, (mx - a.drag_x) * -0.008,
                                            (my - a.drag_y) *  0.008);
                        } else {
                            a.pan_x += mx - a.drag_x;
                            a.pan_y += my - a.drag_y;
                        }
                        a.drag_x = mx; a.drag_y = my;
                    }
                    break;
                }

                case SDL_MOUSEBUTTONDOWN: {
                    float lx, ly;
                    SDL_RenderWindowToLogical(a.ren, e.button.x, e.button.y, &lx, &ly);
                    int mx = (int)lx, my = (int)ly;

                    int h = ui_hit(&a.toolbar, mx, my);
                    SDL_Rect panel = { WIN_W - PANEL_W, 0, PANEL_W, WIN_H };
                    int row = draw_inspector_hit(a.fields, a.nfields, panel, mx, my);

                    a.is_typing = false; a.typing[0] = '\0';

                    /* A press on the scene picks first and orbits only if it
                     * missed, so arranging never requires finding empty space
                     * in a crowded scene. */
                    bool on_canvas = (mx > UI_TOOLBAR_W && mx < WIN_W - PANEL_W);
                    if (h < 0 && row < 0 && on_canvas
                        && a.set.view == OS_VIEW_SCENE && a.R.built) {
                        SDL_Rect cv = { UI_TOOLBAR_W, 0,
                                        WIN_W - UI_TOOLBAR_W - PANEL_W, WIN_H };
                        int po = -1, pl = -1;
                        s3_pick(&a.s3, &a.set.scene, cv.w, cv.h,
                                mx - cv.x, my - cv.y, &po, &pl);
                        if (po >= 0 || pl >= 0) {
                            a.set.sel_obj = po;
                            a.set.sel_light = pl;
                            a.s3_stale = true;
                            vec3 c = (po >= 0) ? a.set.scene.obj[po].centre
                                               : a.set.scene.lit[pl].centre;
                            vec3 hit;
                            if (s3_plane_hit(&a.s3, cv.w, cv.h, mx - cv.x,
                                             my - cv.y, c.y, &hit)) {
                                a.moving = true;
                                a.grab = v3sub(c, hit);
                                a.drag_x = mx; a.drag_y = my;
                            }
                            a.sel_row = -1;
                            break;
                        }
                        /* Clicking empty space clears the selection, so the
                         * panel stops claiming to edit something you can no
                         * longer see the highlight on. */
                        if (a.set.sel_obj >= 0 || a.set.sel_light >= 0) {
                            a.set.sel_obj = a.set.sel_light = -1;
                            a.s3_stale = true;
                        }
                    }

                    if (h >= 0) {
                        a.toolbar.pressed = h;
                    } else if (row >= 0) {
                        a.sel_row = row;
                        const Field *f = &a.fields[row];
                        if (f->is_enum) {
                            /* An enum has nothing to scrub: clicking it steps to
                             * the next value, which is what a list of names
                             * invites. */
                            double nv = fmod(f->value + 1.0, (double)f->nnames);
                            if (os_inspect_set(&a.set, f->id, nv)) apply(&a);
                        } else {
                            a.scrubbing = true;
                            a.scrub_x = mx;
                        }
                    } else if (mx > UI_TOOLBAR_W && mx < WIN_W - PANEL_W) {
                        a.dragging = true; a.drag_x = mx; a.drag_y = my;
                        a.sel_row = -1;
                    }
                    break;
                }

                case SDL_MOUSEBUTTONUP: {
                    float lx, ly;
                    SDL_RenderWindowToLogical(a.ren, e.button.x, e.button.y, &lx, &ly);
                    int mx = (int)lx, my = (int)ly;
                    int h = ui_hit(&a.toolbar, mx, my);
                    if (a.toolbar.pressed >= 0 && h == a.toolbar.pressed)
                        try_action(&a, a.toolbar.buttons[h].action, &helping);
                    a.toolbar.pressed = -1;
                    a.dragging = false;
                    a.scrubbing = false;
                    a.moving = false;
                    break;
                }

                case SDL_MOUSEWHEEL: {
                    ls_real f = e.wheel.y > 0 ? 1.12 : (1.0 / 1.12);
                    if (a.set.view == OS_VIEW_SCENE) {
                        s3_zoom(&a.s3, 1.0 / f);
                        break;
                    }
                    a.zoom *= f;
                    if (a.zoom < 0.25) a.zoom = 0.25;
                    if (a.zoom > 40.0) a.zoom = 40.0;
                    break;
                }
                default: break;
            }
        }

        draw_frame(&a, helping);
        SDL_Delay(16);
    }

    SDL_AtomicSet(&a.R.quit, 1);
    if (a.R.thread) SDL_WaitThread(a.R.thread, NULL);
    renderer_teardown(&a.R);
    if (a.R.lock) SDL_DestroyMutex(a.R.lock);
    if (a.tex) SDL_DestroyTexture(a.tex);
    SDL_DestroyRenderer(a.ren);
    SDL_DestroyWindow(a.win);
    SDL_Quit();
    return 0;
}

/* Offline capture, the Linkage-Design idiom: run the REAL draw path into a
 * hidden software-rendered window and read the pixels back. Every image in the
 * README is produced this way, so a picture cannot drift from the code that
 * made it -- and it is how the views get checked in CI, where there is no
 * display.
 *
 * `subject` selects a configuration rather than a special drawing routine.
 * Drawing anything the interactive path does not draw would defeat the point. */
static int run_capture(const char *subject, const char *dir) {
    App a;
    memset(&a, 0, sizeof a);
    a.sel_row = -1; a.hover_row = -1; a.zoom = 1.0;
    os_settings_default(&a.set);
    s3_init(&a.s3);
    status_init(&a.status);

    if (!strcmp(subject, "singlet")) {
        a.set.lens = OS_LENS_SINGLET_100;
        a.set.chromatic = true; a.set.show_spot = true;
    } else if (!strcmp(subject, "achromat")) {
        a.set.lens = OS_LENS_ACHROMAT_100;
        a.set.chromatic = true; a.set.show_spot = true;
    } else if (!strcmp(subject, "stopped")) {
        a.set.fno = 16.0;
    } else if (!strcmp(subject, "blades")) {
        a.set.blades = 6;
    } else if (!strcmp(subject, "image")) {
        a.set.view = OS_VIEW_IMAGE;
        a.set.res_w = 420;
        a.set.spp = 24;
    } else if (!strcmp(subject, "scene")) {
        a.set.view = OS_VIEW_SCENE;
    } else if (!strcmp(subject, "edit")) {
        a.set.view = OS_VIEW_SCENE;
        a.set.sel_obj = 2;            /* the 2 m target, on the focus plane */
    } else if (!strcmp(subject, "focus457")) {
        a.set.view      = OS_VIEW_IMAGE;
        a.set.focus_m   = 4.57;
        a.set.sensor_w_mm = 41.31;
        a.set.res_w     = 407;
        a.set.coc_limit_mm = 0.056;
        a.set.exposure  = 49.37;
        a.set.spp       = 40;
    } else if (!strcmp(subject, "focus457scene")) {
        a.set.view      = OS_VIEW_SCENE;
        a.set.focus_m   = 4.57;
        a.set.sensor_w_mm = 41.31;
        a.set.coc_limit_mm = 0.056;
    } else if (!strcmp(subject, "ambient")) {
        a.set.view = OS_VIEW_SCENE;
        a.set.scene.light_mode = OS_LIGHT_AMBIENT;
    } else if (!strcmp(subject, "ambientimage")) {
        a.set.view = OS_VIEW_IMAGE;
        a.set.scene.light_mode = OS_LIGHT_AMBIENT;
        a.set.res_w = 420;
        a.set.spp   = 24;
        a.set.exposure = 26.0;
    } else if (!strcmp(subject, "lamp")) {
        a.set.view = OS_VIEW_SCENE;
        a.set.sel_light = 0;
    } else if (strcmp(subject, "wide") != 0) {
        fprintf(stderr, "capture: unknown subject '%s'\n", subject);
        return 2;
    }

    a.win = SDL_CreateWindow("capture", SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H,
                             SDL_WINDOW_HIDDEN);
    if (!a.win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    a.ren = SDL_CreateRenderer(a.win, -1, SDL_RENDERER_SOFTWARE);
    if (!a.ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderDrawBlendMode(a.ren, SDL_BLENDMODE_BLEND);
    SDL_RenderSetLogicalSize(a.ren, WIN_W, WIN_H);

    ui_init(&a.toolbar, WIN_H);
    a.toolbar.hover = -1; a.toolbar.pressed = -1;
    a.R.lock = SDL_CreateMutex();
    a.R.set = a.set;
    if (!renderer_build(&a.R, &a.set)) {
        fprintf(stderr, "capture: %s\n", a.R.why);
        return 1;
    }
    a.plot_stale = true;
    a.s3_stale   = true;

    if (a.set.view == OS_VIEW_IMAGE) {
        /* Rendered on this thread: capture has no interactivity to preserve,
         * and a fixed pass count keeps the image reproducible. */
        OsRenderOpts opt = { a.set.spp, a.set.depth, 0, RENDER_SEED };
        for (int p = 0; p < 12; ++p)
            os_render_pass(&a.R.film, &a.R.cam, &a.R.st, &opt, p);
        SDL_AtomicSet(&a.R.passes, 12);
        tonemap(&a.R);
    }

    /* Settle: frame 0 would otherwise carry a start-up message that has
     * nothing to do with the subject. */
    a.status.count = 0;
    draw_frame(&a, false);

    SDL_Surface *shot = SDL_CreateRGBSurfaceWithFormat(0, WIN_W, WIN_H, 32,
                                                       SDL_PIXELFORMAT_ARGB8888);
    if (!shot) { fprintf(stderr, "surface: %s\n", SDL_GetError()); return 1; }
    if (SDL_RenderReadPixels(a.ren, NULL, SDL_PIXELFORMAT_ARGB8888,
                             shot->pixels, shot->pitch) != 0) {
        fprintf(stderr, "readpixels: %s\n", SDL_GetError());
        return 1;
    }
    char path[512];
    snprintf(path, sizeof path, "%s/%s.bmp", dir, subject);
    if (SDL_SaveBMP(shot, path) != 0) {
        fprintf(stderr, "save %s: %s\n", path, SDL_GetError());
        return 1;
    }
    printf("wrote %s\n", path);

    SDL_FreeSurface(shot);
    renderer_teardown(&a.R);
    if (a.R.lock) SDL_DestroyMutex(a.R.lock);
    if (a.tex) SDL_DestroyTexture(a.tex);
    SDL_DestroyRenderer(a.ren);
    SDL_DestroyWindow(a.win);
    return 0;
}
