/* VENDORED from Light-Simulation @ 2a1c9ec, MIT, same author.
 * CHANGED: the auto-exposure branch in ls_film_write_ppm is deleted; a
 *          non-positive `exposure` is now rejected instead of triggering a
 *          99th-percentile stretch. See the comment at the deletion site. */
#include "lightsim/film.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

bool ls_film_init(Film *f, int width, int height) {
    f->width = width;
    f->height = height;
    f->pix = calloc((size_t)width * (size_t)height, sizeof *f->pix);
    f->n   = calloc((size_t)width * (size_t)height, sizeof *f->n);
    return f->pix != NULL && f->n != NULL;
}

void ls_film_free(Film *f) {
    free(f->pix); free(f->n);
    f->pix = NULL; f->n = NULL;
}

void ls_film_add(Film *f, int x, int y, const Spectrum *L) {
    size_t i = (size_t)y * (size_t)f->width + (size_t)x;
    ls_acc_add_scaled(&f->pix[i], L, 1.0);
    f->n[i]++;
}

Spectrum ls_film_mean(const Film *f, int x, int y) {
    size_t i = (size_t)y * (size_t)f->width + (size_t)x;
    return ls_acc_mean(&f->pix[i], f->n[i]);
}

/* cmp_real() lived here to sort luminances for the auto-exposure percentile.
 * It went with the branch it served. */

bool ls_film_write_ppm(const Film *f, const char *path, ls_real exposure) {
    size_t np = (size_t)f->width * (size_t)f->height;
    RGB *lin = malloc(np * sizeof *lin);
    if (!lin) return false;

    for (int y = 0; y < f->height; ++y)
        for (int x = 0; x < f->width; ++x) {
            Spectrum s = ls_film_mean(f, x, y);
            lin[(size_t)y * (size_t)f->width + (size_t)x] =
                ls_xyz_to_linear_srgb(ls_spectrum_to_xyz(&s));
        }

    /* The auto-exposure branch that stood here -- sort the luminances, put the
     * 99th percentile at white -- is DELETED, not disabled. In a lighting
     * simulator it is a convenience. In a camera simulator it is a lie: it
     * silently renormalises every image, so an f-stop, a shutter time and an
     * ISO could all be wrong together and the picture would still come out
     * looking correctly exposed. Brightness here comes from the exposure
     * triangle by way of os_sensor.c, and from nothing else.
     *
     * It is deleted rather than commented out so that check-exposure-purity in
     * the Makefile can never find the words again. A non-positive exposure is
     * now a caller bug, and says so, instead of quietly auto-exposing. */
    if (!(exposure > 0.0)) return false;   /* also rejects NaN */

    FILE *fp = fopen(path, "wb");
    if (!fp) { free(lin); return false; }
    fprintf(fp, "P6\n%d %d\n255\n", f->width, f->height);
    for (size_t i = 0; i < np; ++i) {
        RGB c = { lin[i].r * exposure, lin[i].g * exposure, lin[i].b * exposure };
        c = ls_rgb_gamma_encode(c);
        unsigned char rgb[3];
        rgb[0] = (unsigned char)(ls_clamp(c.r, 0.0, 1.0) * 255.0 + 0.5);
        rgb[1] = (unsigned char)(ls_clamp(c.g, 0.0, 1.0) * 255.0 + 0.5);
        rgb[2] = (unsigned char)(ls_clamp(c.b, 0.0, 1.0) * 255.0 + 0.5);
        fwrite(rgb, 1, 3, fp);
    }
    fclose(fp);
    free(lin);
    return true;
}

bool ls_film_write_pfm(const Film *f, const char *path) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return false;
    /* Negative scale marks little-endian, which is what arm64 and x86 write. */
    fprintf(fp, "PF\n%d %d\n-1.0\n", f->width, f->height);
    for (int y = f->height - 1; y >= 0; --y)       /* PFM rows run bottom-up */
        for (int x = 0; x < f->width; ++x) {
            Spectrum s = ls_film_mean(f, x, y);
            RGB c = ls_xyz_to_linear_srgb(ls_spectrum_to_xyz(&s));
            float v[3] = { (float)c.r, (float)c.g, (float)c.b };
            fwrite(v, sizeof(float), 3, fp);
        }
    fclose(fp);
    return true;
}

/* Viridis control points (matplotlib), linearly interpolated. Perceptually
 * uniform and legible in greyscale, which a rainbow ramp is not. */
static const ls_real VIRIDIS[][3] = {
    {0.267004,0.004874,0.329415},{0.282623,0.140926,0.457517},
    {0.253935,0.265254,0.529983},{0.206756,0.371758,0.553117},
    {0.163625,0.471133,0.558148},{0.127568,0.566949,0.550556},
    {0.134692,0.658636,0.517649},{0.266941,0.748751,0.440573},
    {0.477504,0.821444,0.318195},{0.741388,0.873449,0.149561},
    {0.993248,0.906157,0.143936}
};

RGB ls_colormap_viridis(ls_real t) {
    const int N = (int)(sizeof VIRIDIS / sizeof VIRIDIS[0]);
    t = ls_clamp(t, 0.0, 1.0) * (ls_real)(N - 1);
    int i = (int)t;
    if (i >= N - 1) i = N - 2;
    ls_real u = t - (ls_real)i;
    RGB c;
    c.r = ls_lerp(u, VIRIDIS[i][0], VIRIDIS[i+1][0]);
    c.g = ls_lerp(u, VIRIDIS[i][1], VIRIDIS[i+1][1]);
    c.b = ls_lerp(u, VIRIDIS[i][2], VIRIDIS[i+1][2]);
    return c;
}

bool ls_write_falsecolor_ppm(const char *path, const ls_real *v, int nu, int nv,
                             ls_real lo, ls_real hi, int upscale) {
    if (upscale < 1) upscale = 1;
    FILE *fp = fopen(path, "wb");
    if (!fp) return false;
    int W = nu * upscale, H = nv * upscale;
    fprintf(fp, "P6\n%d %d\n255\n", W, H);
    ls_real span = (hi > lo) ? (hi - lo) : 1.0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            int cu = x / upscale, cv = y / upscale;
            RGB c = ls_colormap_viridis((v[(size_t)cv * (size_t)nu + (size_t)cu] - lo) / span);
            unsigned char rgb[3];
            /* The ramp is already perceptual; encode straight to sRGB bytes. */
            rgb[0] = (unsigned char)(ls_clamp(c.r, 0.0, 1.0) * 255.0 + 0.5);
            rgb[1] = (unsigned char)(ls_clamp(c.g, 0.0, 1.0) * 255.0 + 0.5);
            rgb[2] = (unsigned char)(ls_clamp(c.b, 0.0, 1.0) * 255.0 + 0.5);
            fwrite(rgb, 1, 3, fp);
        }
    fclose(fp);
    return true;
}
