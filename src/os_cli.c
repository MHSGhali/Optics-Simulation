/* os_cli.c — the command-line face of opticsim.
 *
 * Subcommands are dispatched by strcmp against argv[1], and each one owns its
 * own flag parsing. There is no options framework: the flag sets barely
 * overlap, and a shared parser would have to accept --focus on a subcommand
 * that has no lens.
 *
 * Kept free of SDL so that the whole command-line side of the program builds,
 * runs and is tested on a machine with no windowing library at all. The single
 * `opticsim` binary opens a window when it is given no subcommand and behaves
 * as a batch tool when it is given one; this file is the second half of that,
 * and viewer/ is the first.
 *
 * Every subcommand that will exist is listed in COMMANDS below, including the
 * ones not yet implemented, which report the milestone that brings them. A
 * usage message that lies by omission is worse than one that says "not yet":
 * the first looks like the feature was never planned. */
#include "opticsim/cli.h"
#include "lightsim/spectrum.h"
#include "lightsim/color.h"
#include "lightsim/units.h"
#include "opticsim/glass.h"
#include "opticsim/camera.h"
#include "opticsim/render.h"
#include "opticsim/scenedesc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int (*CmdFn)(int argc, char **argv);

static int cmd_spectrum(int argc, char **argv);
static int cmd_glass(int argc, char **argv);
static int cmd_still(int argc, char **argv);
static int cmd_todo(int argc, char **argv);

typedef struct {
    const char *name;
    CmdFn       fn;
    const char *summary;
    const char *milestone;   /* NULL once implemented */
} Command;

static const Command COMMANDS[] = {
    { "spectrum", cmd_spectrum, "report the spectral band and the colour pipeline", NULL },
    { "glass",    cmd_glass,    "Sellmeier index and Abbe number per catalogue glass", NULL },
    { "lens",     cmd_todo,     "paraxial report, spot diagram, vignetting, MTF",      "M3" },
    { "sensor",   cmd_todo,     "format presets: pitch, full well, implied ISO",       "M8" },
    { "still",    cmd_still,    "render one frame through the real lens",              NULL },
    { "frames",   cmd_todo,     "render a sequence with camera and subject motion",    "M11" },
    { "mtf",      cmd_todo,     "slanted-edge MTF50 in cycles/mm",                     "M15" },
};
static const int NCOMMANDS = (int)(sizeof COMMANDS / sizeof COMMANDS[0]);

void os_cli_usage(const char *argv0) {
    printf("usage: %s                    open the interactive viewer\n", argv0);
    printf("       %s <command> [options]\n\n", argv0);
    for (int i = 0; i < NCOMMANDS; ++i) {
        printf("  %-9s %s", COMMANDS[i].name, COMMANDS[i].summary);
        if (COMMANDS[i].milestone) printf("   [not yet: %s]", COMMANDS[i].milestone);
        printf("\n");
    }
    printf("\nRun with no command at all to open the viewer, where every one\n"
           "of these settings is a control rather than a flag.\n");
}

bool os_cli_is_command(const char *s) {
    for (int i = 0; i < NCOMMANDS; ++i)
        if (strcmp(COMMANDS[i].name, s) == 0) return true;
    return false;
}

int os_cli_run(int argc, char **argv) {
    for (int i = 0; i < NCOMMANDS; ++i)
        if (strcmp(COMMANDS[i].name, argv[0]) == 0)
            return COMMANDS[i].fn(argc, argv);
    return 2;
}

/* A placeholder that names its milestone and exits non-zero, so a script that
 * calls it fails loudly instead of proceeding on empty output. */
static int cmd_todo(int argc, char **argv) {
    (void)argc;
    for (int i = 0; i < NCOMMANDS; ++i)
        if (strcmp(COMMANDS[i].name, argv[0]) == 0) {
            fprintf(stderr, "%s: not implemented yet (%s: %s)\n",
                    argv[0], COMMANDS[i].milestone, COMMANDS[i].summary);
            return 3;
        }
    return 3;
}

static int cmd_spectrum(int argc, char **argv) {
    (void)argc; (void)argv;

    printf("spectral band   %d..%d nm at %d nm  (%d bins)\n",
           LS_LAMBDA_MIN_NM, LS_LAMBDA_MAX_NM, LS_SPECTRAL_STEP_NM, LS_NBINS);

    /* The lumen's definition, printed rather than merely asserted in a test:
     * one watt at 555 nm is 683 lm. If this line is wrong, every photometric
     * number the program later prints is wrong by the same factor. */
    Spectrum m = ls_spectrum_monochromatic(555.0, 1.0);
    printf("1 W at 555 nm   %.4f W   %.2f lm   (683.00 lm/W by definition)\n",
           ls_radiometric(&m), ls_photometric(&m));

    printf("\n  %-8s %8s %8s %8s   %s\n", "source", "x", "y", "lm/W", "");
    struct { const char *name; ls_real cct; } sources[] = {
        { "candle",   1850.0 }, { "tungsten", 2856.0 }, { "warm LED", 3000.0 },
        { "daylight", 5500.0 }, { "overcast", 6500.0 }, { "blue sky", 9000.0 },
    };
    for (int i = 0; i < (int)(sizeof sources / sizeof sources[0]); ++i) {
        Spectrum s = ls_spectrum_blackbody(sources[i].cct);
        ls_real x, y;
        ls_xyz_chromaticity(ls_spectrum_to_xyz(&s), &x, &y);
        printf("  %-8s %8.4f %8.4f %8.1f   %.0f K\n",
               sources[i].name, x, y, ls_luminous_efficacy_band(&s), sources[i].cct);
    }
    printf("\nChromaticity x falls as the source gets hotter, which is the\n"
           "Planckian locus running from red through white toward blue.\n");
    return 0;
}

static int cmd_glass(int argc, char **argv) {
    (void)argc; (void)argv;

    /* The audit runs first and refuses to print on failure. A glass table that
     * does not reproduce its own published constants is worse than no table:
     * every index it yields is plausible, so the error surfaces later as a lens
     * that mysteriously will not focus colour. */
    char why[256];
    if (!os_glass_self_check(why, sizeof why)) {
        fprintf(stderr, "glass: catalogue self-check FAILED -- %s\n", why);
        return 1;
    }
    printf("catalogue self-check: every glass reproduces its published n_d and V_d\n\n");

    printf("  %-9s %8s %7s %8s %8s %8s %9s\n",
           "glass", "n_d", "V_d", "n(F)", "n(d)", "n(C)", "n_F-n_C");
    printf("  %-9s %8s %7s %8s %8s %8s %9s\n",
           "", "", "", "486nm", "588nm", "656nm", "");
    for (int id = 0; id < OS_GLASS_COUNT; ++id) {
        const OsGlass *g = os_glass((OsGlassId)id);
        printf("  %-9s %8.5f %7.2f %8.5f %8.5f %8.5f %9.5f\n",
               g->name, os_glass_n(g, OS_LINE_D), os_glass_abbe(g),
               os_glass_n(g, OS_LINE_F), os_glass_n(g, OS_LINE_D),
               os_glass_n(g, OS_LINE_C), os_glass_dispersion(g));
    }

    printf("\nA high V_d is a crown (weak dispersion), a low V_d a flint.\n"
           "Pairing one of each is what makes an achromatic doublet possible,\n"
           "and n_F > n_C everywhere is why blue focuses short of red.\n");
    return 0;
}

static int cmd_still(int argc, char **argv) {
    /* Defaults chosen so a bare `opticsim still` shows the thing this program
     * is for: a rail of targets at known depths, focused on the one at 2 m,
     * wide enough open that the others are visibly not. */
    const char *out = "out/still.ppm";
    const char *pfm = NULL;
    OsPrescriptionId lens = OS_LENS_ACHROMAT_100;
    OsStageId stage = OS_STAGE_DEPTH_RAIL;
    ls_real focal = 100.0, fno = 5.0, focus = 2.0, sensor_w = 36.0;
    ls_real exposure = 0.0;          /* 0 => derive from the scene, see below */
    int w = 480, h = 320, spp = 64, passes = 1, depth = 6, threads = 0;
    int blades = -1;   /* -1 keeps the lens's own default */
    /* Lit by the preset's own lamps unless a dome is asked for. --ambient
     * takes the illuminance a surface facing the sky receives, in lux, and
     * switching to it turns the lamps off -- the same either/or the viewer
     * offers, because a half-and-half scene answers neither question. */
    ls_real ambient_lx = -1.0, ambient_k = 6500.0;

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
        #define NEEDS(x) do { if (!v) { fprintf(stderr, "still: %s needs a value\n", x); return 2; } } while (0)
        if      (!strcmp(a, "--out"))      { NEEDS(a); out = v; i++; }
        else if (!strcmp(a, "--pfm"))      { NEEDS(a); pfm = v; i++; }
        else if (!strcmp(a, "--focal"))    { NEEDS(a); focal = atof(v); i++; }
        else if (!strcmp(a, "--fstop"))    { NEEDS(a); fno = atof(v); i++; }
        else if (!strcmp(a, "--focus"))    { NEEDS(a); focus = atof(v); i++; }
        else if (!strcmp(a, "--sensor"))   { NEEDS(a); sensor_w = atof(v); i++; }
        else if (!strcmp(a, "--spp"))      { NEEDS(a); spp = atoi(v); i++; }
        else if (!strcmp(a, "--passes"))   { NEEDS(a); passes = atoi(v); i++; }
        else if (!strcmp(a, "--depth"))    { NEEDS(a); depth = atoi(v); i++; }
        else if (!strcmp(a, "--threads"))  { NEEDS(a); threads = atoi(v); i++; }
        else if (!strcmp(a, "--exposure")) { NEEDS(a); exposure = atof(v); i++; }
        else if (!strcmp(a, "--blades"))   { NEEDS(a); blades = atoi(v); i++; }
        else if (!strcmp(a, "--width"))    { NEEDS(a); w = atoi(v); i++; }
        else if (!strcmp(a, "--height"))   { NEEDS(a); h = atoi(v); i++; }
        else if (!strcmp(a, "--ambient"))  { NEEDS(a); ambient_lx = atof(v); i++; }
        else if (!strcmp(a, "--sky-k"))    { NEEDS(a); ambient_k = atof(v); i++; }
        else if (!strcmp(a, "--lens")) {
            NEEDS(a);
            if      (!strcmp(v, "singlet"))  lens = OS_LENS_SINGLET_100;
            else if (!strcmp(v, "achromat")) lens = OS_LENS_ACHROMAT_100;
            else if (!strcmp(v, "thin"))     lens = OS_LENS_THIN;
            else if (!strcmp(v, "zoom"))     lens = OS_LENS_ZOOM_RETRO;
            else { fprintf(stderr, "still: unknown lens '%s'\n", v); return 2; }
            i++;
        }
        else if (!strcmp(a, "--stage")) {
            NEEDS(a);
            if      (!strcmp(v, "rail"))  stage = OS_STAGE_DEPTH_RAIL;
            else if (!strcmp(v, "ring"))  stage = OS_STAGE_DEPTH_RING;
            else if (!strcmp(v, "bokeh")) stage = OS_STAGE_BOKEH;
            else if (!strcmp(v, "grid"))  stage = OS_STAGE_GRID;
            else { fprintf(stderr, "still: unknown stage '%s'\n", v); return 2; }
            i++;
        }
        else { fprintf(stderr, "still: unknown option '%s'\n", a); return 2; }
        #undef NEEDS
    }

    /* The CLI renders a preset as-authored: there is no UI here to arrange
     * anything, so it seeds a description and builds that. Same path the
     * viewer uses, so the two cannot render different scenes. */
    OsSceneDesc desc;
    os_scenedesc_preset(&desc, stage);
    if (ambient_lx >= 0.0) {
        desc.light_mode    = OS_LIGHT_AMBIENT;
        desc.ambient_lux   = ambient_lx;
        desc.ambient_cct_k = ambient_k;
    }
    OsStage st;
    if (!os_scenedesc_build(&desc, &st)) {
        fprintf(stderr, "still: could not build the %s stage\n",
                os_stage_name(stage));
        return 1;
    }

    char why[256];
    OsCamera cam;
    if (!os_camera_build(&cam, lens, focal, fno, sensor_w, w, h, why, sizeof why)) {
        fprintf(stderr, "still: %s\n", why);
        os_stage_free(&st);
        return 1;
    }
    if (blades >= 0) cam.lens.blades = blades;
    /* SAY SO when the lens cannot do what was asked. os_lens_focus refuses a
     * subject inside the front focal point and leaves the film where it was --
     * at infinity, straight out of the build -- so the frame comes out focused
     * somewhere else entirely. This used to be discarded, and the line below
     * then printed the REQUESTED distance over a picture that did not have it:
     * `--focal 400 --focus 0.2` said "focus 0.20 m" above a frame focused at
     * infinity. */
    if (!os_lens_focus(&cam.lens, focus))
        fprintf(stderr, "still: cannot focus at %.3g m -- that is inside this "
                        "lens's front focal point; focused at infinity "
                        "instead\n", (double)focus);
    os_camera_refresh(&cam);
    os_camera_look_at(&cam, st.cam_eye, st.cam_target, v3(0.0, 1.0, 0.0));

    /* Reported from the LENS, not from the request, for the same reason the
     * f-number is: both can be clamped or refused, and a report that echoes
     * the flag cannot show it. */
    {
        char fdist[32];
        if (isfinite(cam.lens.focus_distance_m))
            snprintf(fdist, sizeof fdist, "%.2f m",
                     (double)cam.lens.focus_distance_m);
        else
            snprintf(fdist, sizeof fdist, "infinity");
        printf("lens      %s  %.1f mm  f/%.1f  focus %s\n",
               cam.lens.name, (double)cam.lens.efl_mm,
               (double)cam.lens.f_number, fdist);
    }
    /* Distortion at the frame corner, reported beside the lens rather than
     * buried, because it is the one aberration a still cannot show you: it
     * moves image points instead of blurring them, so it is invisible unless
     * the scene has something that ought to be straight. Render --stage grid
     * to see it; this is the number. */
    {
        ls_real half_diag = 0.5 * sqrt(cam.sensor_w_mm * cam.sensor_w_mm
                                     + cam.sensor_h_mm * cam.sensor_h_mm);
        ls_real d = os_lens_distortion_pct(&cam.lens, half_diag);
        if (isfinite(d))
            printf("distortion %+.3f %% at the corner (%.1f mm off axis) -- %s\n",
                   (double)d, (double)half_diag,
                   d > 0.0 ? "pincushion" : "barrel");
        else
            printf("distortion  the corner is vignetted; no chief ray gets "
                   "through\n");
    }
    printf("sensor    %.1f x %.1f mm   %d x %d px   %.1f deg horizontal\n",
           (double)cam.sensor_w_mm, (double)cam.sensor_h_mm, w, h,
           (double)os_camera_hfov_deg(&cam));
    printf("sampling  %d spp x %d pass = %d, depth %d\n",
           spp, passes, spp * passes, depth);
    if (desc.light_mode == OS_LIGHT_AMBIENT)
        printf("lighting  ambient dome, %.0f lx at %.0f K -- lamps off\n",
               (double)desc.ambient_lux, (double)desc.ambient_cct_k);
    else
        printf("lighting  %d placed lamp(s)\n", st.scene.nlights);

    Film film;
    if (!ls_film_init(&film, w, h)) { fprintf(stderr, "still: out of memory\n"); return 1; }

    OsRenderOpts opt = { spp, depth, threads, 0x853C49E6748FEA9Bull };
    for (int p = 0; p < passes; ++p) {
        os_render_pass(&film, &cam, &st, &opt, p);
        printf("  pass %d/%d\r", p + 1, passes);
    }
    printf("\n");

    /* What the film actually holds, reported before anything is scaled.
     *
     * This is not auto-exposure -- nothing here changes the image. It is the
     * measurement that makes choosing an exposure a decision rather than a
     * guess, and it is the first thing to look at when a render comes out
     * black: a mean of zero is a geometry or lighting problem, and a mean of
     * 1e-6 is an exposure problem. The two look identical on screen. */
    {
        ls_real lo = HUGE_VAL, hi = 0.0, sum = 0.0;
        int n = 0;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                Spectrum sp = ls_film_mean(&film, x, y);
                XYZ c = ls_spectrum_to_xyz(&sp);
                ls_real Y = c.y;                  /* luminance */
                if (Y < lo) lo = Y;
                if (Y > hi) hi = Y;
                sum += Y; n++;
            }
        ls_real mean = n ? sum / (ls_real)n : 0.0;
        printf("film      luminance  min %.4g  mean %.4g  max %.4g\n",
               (double)lo, (double)mean, (double)hi);
        if (hi > 0.0)
            printf("          an exposure near %.3g puts the brightest pixel at white\n",
                   (double)(1.0 / hi));
    }

    /* Exposure is an EXPLICIT scale, never derived from the image's own
     * histogram. The vendored film writer's percentile stretch was deleted for
     * this reason: an image that renormalises itself looks correctly exposed
     * however wrong the f-number and shutter were. Until the sensor model
     * lands and brightness comes from the exposure triangle, the number is
     * simply a flag with a documented default.
     *
     * 100 is not arbitrary: the shipped stages are lit to roughly a 200 lux
     * interior, a 0.75-albedo surface under that returns about
     * 200 x 0.75 / pi = 48 cd/m^2, and at f/5 the film sees around 0.01 in the
     * units the film stores. Scaling by 100 puts that near white. When the
     * sensor lands, this constant is replaced by the exposure triangle and the
     * flag becomes a viewing gain rather than a fudge. */
    if (exposure <= 0.0) exposure = 100.0;

    if (!ls_film_write_ppm(&film, out, exposure)) {
        fprintf(stderr, "still: cannot write %s (does its directory exist?)\n", out);
        ls_film_free(&film);
        os_camera_free(&cam);
        os_stage_free(&st);
        return 1;
    }
    printf("wrote %s   (exposure x%.3g)\n", out, (double)exposure);

    /* The film in PHYSICAL units, untouched by exposure or by the sRGB
     * transfer. Measuring optics off an 8-bit tone-mapped image means
     * measuring the tone map: a fixed brightness threshold picks out a
     * different fraction of a blur disc depending on how concentrated the
     * light is, so the same lens appears to have a different circle of
     * confusion at every aperture. This is the output the tests measure. */
    if (pfm && !ls_film_write_pfm(&film, pfm))
        fprintf(stderr, "still: cannot write %s\n", pfm);
    else if (pfm)
        printf("wrote %s   (raw float, physical units)\n", pfm);

    ls_film_free(&film);
    os_camera_free(&cam);
    os_stage_free(&st);
    return 0;
}

