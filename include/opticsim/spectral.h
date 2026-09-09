/* spectral.h — one wavelength per camera path.
 *
 * THE INVARIANT THIS MODULE OWNS
 *   Every camera sample carries exactly one wavelength, drawn with a strictly
 *   positive probability, and the reciprocal of that probability travels with
 *   it. Nothing downstream may deposit into a bin it did not sample.
 *
 * WHY A PATH CANNOT CARRY THE WHOLE SPECTRUM
 *   lightsim/spectrum.h is built on a fixed 95-bin Spectrum travelling down one
 *   geometric path, which is valid because every SCENE material is
 *   non-dispersive. That invariant still holds. What broke is upstream of it:
 *   the LENS is dispersive, so ray GENERATION became wavelength dependent. Two
 *   rays leaving the rear element from the same sensor point at 450 nm and
 *   650 nm are different rays, going to different places in the world.
 *
 *   Carrying 95 bins down one of them and depositing all 95 would average the
 *   scene over wavelengths that never travelled there -- which is precisely how
 *   you erase the chromatic aberration the lens exists to produce. So: one
 *   wavelength, one ray, one bin.
 *
 * WHY BIN CENTRES RATHER THAN A CONTINUOUS DRAW
 *   The wavelength is always a bin CENTRE. Depositing then needs no
 *   redistribution between neighbours, and reading a Spectrum back at that
 *   wavelength with ls_spectrum_at() is exact to the last bit. Sampling
 *   continuously would gain nothing -- the scene's own spectra are binned at
 *   the same resolution -- and would cost an off-by-half-a-bin bug class.
 *
 * WHY STRATIFIED RATHER THAN INDEPENDENT
 *   Independent uniform draws leave gaps and clumps in the spectrum for any
 *   one pixel, which reads as colour noise. A golden-ratio (additive
 *   recurrence) sequence over the sample index walks the band evenly, is
 *   stateless -- so it survives being restarted and split across threads -- and
 *   costs one multiply.
 */
#ifndef OPTICSIM_SPECTRAL_H
#define OPTICSIM_SPECTRAL_H

#include "lightsim/spectrum.h"

typedef struct {
    ls_real lambda_nm;   /* exactly ls_bin_lambda(bin)                        */
    int     bin;
    ls_real inv_pdf;     /* 1/P(bin); LS_NBINS while the draw is uniform      */
} OsWavelength;

/* Draw the hero wavelength for one sample.
 *
 * `sample_index` counts samples within the pixel across every pass, so the
 * stratification keeps improving as a render refines instead of restarting.
 * `pixel_hash` decorrelates neighbouring pixels -- without it every pixel walks
 * the spectrum in lockstep and the residual noise becomes a visible pattern
 * rather than grain. */
OsWavelength os_lambda_pick(uint64_t sample_index, uint32_t pixel_hash);

/* A cheap, well-mixed hash of a pixel coordinate. */
uint32_t os_pixel_hash(int x, int y);

#endif /* OPTICSIM_SPECTRAL_H */
