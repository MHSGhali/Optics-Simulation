/* test_spectral.c — the vendored spectral/colour core, proven in its new home,
 * plus the one function opticsim added to it.
 *
 * This suite is not a re-run of Light-Simulation's tests for their own sake.
 * It exists because vendoring copies code out from under its original build:
 * a different -O level, a different LS_SPECTRAL_STEP_NM, or a botched copy
 * would all still compile. These are the invariants everything downstream
 * leans on, asserted against closed forms rather than against the old repo. */
#include "test.h"
#include "tests.h"

#include "lightsim/spectrum.h"
#include "lightsim/color.h"
#include "lightsim/units.h"

void os_test_spectral(void) {
    SECTION("spectrum: ls_spectrum_at (new for opticsim)");
    {
        /* A ramp, so every bin holds a different, predictable value. */
        Spectrum s = ls_spectrum_zero();
        for (int i = 0; i < LS_NBINS; ++i) s.v[i] = (float)i;

        /* Exact at every bin centre. The hero-wavelength estimator samples bin
         * centres and then reads back with ls_spectrum_at; if that round trip
         * were not exact the deposit would be biased at every wavelength. */
        for (int i = 0; i < LS_NBINS; ++i)
            CHECK(ls_spectrum_at(&s, ls_bin_lambda(i)) == (ls_real)s.v[i]);

        /* Halfway between bins 10 and 11 is exactly 10.5 on this ramp. */
        ls_real mid = 0.5 * (ls_bin_lambda(10) + ls_bin_lambda(11));
        CHECK_NEAR(ls_spectrum_at(&s, mid), 10.5, 1e-12);

        /* A quarter of the way is 10.25 -- proves it is linear, not nearest. */
        ls_real q = ls_bin_lambda(10) + 0.25 * LS_SPECTRAL_STEP;
        CHECK_NEAR(ls_spectrum_at(&s, q), 10.25, 1e-12);

        /* Clamped, never extrapolated. Extrapolating a falling tail below the
         * band goes negative, and a negative radiance becomes a NaN as soon as
         * anything takes its square root. */
        CHECK(ls_spectrum_at(&s, LS_LAMBDA_MIN - 100.0) == (ls_real)s.v[0]);
        CHECK(ls_spectrum_at(&s, LS_LAMBDA_MAX + 100.0) == (ls_real)s.v[LS_NBINS - 1]);
        CHECK(ls_spectrum_at(&s, LS_LAMBDA_MIN) == (ls_real)s.v[0]);
        CHECK(ls_spectrum_at(&s, LS_LAMBDA_MAX) == (ls_real)s.v[LS_NBINS - 1]);

        /* Monotone on a monotone table, at 1 nm steps across the whole band. */
        ls_real prev = -1.0;
        for (ls_real l = LS_LAMBDA_MIN; l <= LS_LAMBDA_MAX; l += 1.0) {
            ls_real v = ls_spectrum_at(&s, l);
            CHECK(v >= prev - 1e-12);
            prev = v;
        }
    }

    SECTION("spectrum: integration carries stated power");
    {
        /* Tolerance policy, inherited from the vendored code: 1e-6 for any
         * quantity that passed through a float Spectrum bin, 1e-12 for pure
         * double arithmetic. 3.0/5.0 is not representable in float, so the
         * round trip through v[i] lands ~4e-8 out -- that is float epsilon
         * doing its job, not an integration error.
         *
         * ls_spectrum_monochromatic stores power/step so the band integral is
         * exactly power. Every radiometric claim downstream rests on this. */
        Spectrum m = ls_spectrum_monochromatic(555.0, 3.0);
        CHECK_NEAR(ls_spectrum_integrate(&m), 3.0, 1e-6);

        /* Including in the EDGE bins -- the rectangle rule was chosen over the
         * trapezoid rule precisely so power does not vanish at the band edges. */
        Spectrum lo = ls_spectrum_monochromatic(LS_LAMBDA_MIN, 3.0);
        Spectrum hi = ls_spectrum_monochromatic(LS_LAMBDA_MAX, 3.0);
        CHECK_NEAR(ls_spectrum_integrate(&lo), 3.0, 1e-6);
        CHECK_NEAR(ls_spectrum_integrate(&hi), 3.0, 1e-6);

        /* A constant of 1/(band width) integrates to 1. */
        ls_real width = (ls_real)(LS_NBINS) * LS_SPECTRAL_STEP;
        Spectrum c = ls_spectrum_const(1.0 / width);
        CHECK_NEAR(ls_spectrum_integrate(&c), 1.0, 1e-6);
    }

    SECTION("units: 683 lm/W at 555 nm");
    {
        /* The definition of the lumen. 1 W of 555 nm light is 683 lm exactly,
         * so this simultaneously checks the CMF table, the bin width and Km. */
        Spectrum m = ls_spectrum_monochromatic(555.0, 1.0);
        CHECK_NEAR(ls_radiometric(&m), 1.0, 1e-6);
        CHECK_NEAR(ls_photometric(&m), LS_KM_LM_PER_W, 2e-3);

        /* Well outside the eye's response, a watt is worth almost no lumens. */
        Spectrum ir = ls_spectrum_monochromatic(800.0, 1.0);
        CHECK(ls_photometric(&ir) < 1.0);
        CHECK(ls_photometric(&ir) >= 0.0);
    }

    SECTION("colour: equal-energy white sits at the white point");
    {
        /* An equal-energy (illuminant E) spectrum has chromaticity (1/3, 1/3)
         * by construction, whatever the CMF normalisation. */
        Spectrum e = ls_spectrum_const(1.0);
        XYZ xyz = ls_spectrum_to_xyz(&e);
        ls_real x, y;
        ls_xyz_chromaticity(xyz, &x, &y);
        CHECK_NEAR(x, 1.0 / 3.0, 2e-3);
        CHECK_NEAR(y, 1.0 / 3.0, 2e-3);
    }

    SECTION("colour: sRGB transfer round trip");
    {
        /* ls_srgb_encode is applied to every pixel this program ever writes. */
        CHECK_NEAR(ls_srgb_encode(0.0), 0.0, 1e-12);
        CHECK_NEAR(ls_srgb_encode(1.0), 1.0, 1e-12);
        /* The standard's own reference point: linear 0.0031308 is the knee
         * between the linear segment and the power segment, at 12.92x. */
        CHECK_NEAR(ls_srgb_encode(0.0031308), 0.0031308 * 12.92, 1e-9);
        /* Monotone, and inside [0,1] for inputs inside [0,1]. */
        ls_real prev = -1.0;
        for (int i = 0; i <= 100; ++i) {
            ls_real v = ls_srgb_encode((ls_real)i / 100.0);
            CHECK(v >= prev);
            CHECK(v >= 0.0 && v <= 1.0);
            prev = v;
        }
    }

    SECTION("spectrum: blackbody obeys Wien and Stefan-Boltzmann");
    {
        /* Wien's displacement law: peak at 2.897771955e6 nm.K / T. At 3000 K
         * that is 965.9 nm -- outside our band, so the in-band spectrum must be
         * monotonically RISING. At 8000 K the peak is 362.2 nm, just inside the
         * band, so the spectrum must turn over. Two temperatures, opposite
         * shapes, one law. */
        Spectrum warm = ls_spectrum_blackbody(3000.0);
        bool rising = true;
        for (int i = 1; i < LS_NBINS; ++i)
            if (warm.v[i] < warm.v[i - 1]) rising = false;
        CHECK(rising);

        Spectrum hot = ls_spectrum_blackbody(8000.0);
        int peak = 0;
        for (int i = 1; i < LS_NBINS; ++i) if (hot.v[i] > hot.v[peak]) peak = i;
        CHECK_NEAR(ls_bin_lambda(peak), 2.897771955e6 / 8000.0, 0.02);

        /* Stefan-Boltzmann, as a ratio so the constant cancels: doubling T
         * multiplies total radiance by exactly 16. */
        ls_real a = ls_blackbody_total_radiance(1000.0);
        ls_real b = ls_blackbody_total_radiance(2000.0);
        CHECK_NEAR(b / a, 16.0, 1e-12);
        CHECK_NEAR(a, LS_STEFAN_BOLTZMANN * 1e12 / LS_PI, 1e-12);

        /* Hotter is bluer: chromaticity x must fall as temperature rises. */
        ls_real x1, y1, x2, y2;
        ls_xyz_chromaticity(ls_spectrum_to_xyz(&warm), &x1, &y1);
        ls_xyz_chromaticity(ls_spectrum_to_xyz(&hot),  &x2, &y2);
        CHECK(x2 < x1);
    }

    SECTION("spectrum: accumulator means what it says");
    {
        /* The film is a SpectrumAcc. If the mean were wrong every image would
         * be wrong by a constant, which is the hardest kind of error to see. */
        SpectrumAcc acc = ls_acc_zero();
        Spectrum one = ls_spectrum_const(2.0);
        for (int k = 0; k < 7; ++k) ls_acc_add_scaled(&acc, &one, 1.0);
        Spectrum mean = ls_acc_mean(&acc, 7);
        for (int i = 0; i < LS_NBINS; ++i) CHECK_NEAR(mean.v[i], 2.0, 1e-12);

        /* Zero samples is black, not a division by zero. */
        SpectrumAcc empty = ls_acc_zero();
        Spectrum z = ls_acc_mean(&empty, 0);
        CHECK(ls_spectrum_is_black(&z));
    }
}
