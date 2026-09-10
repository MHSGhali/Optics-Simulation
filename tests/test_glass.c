/* test_glass.c — the dispersion data, audited against its own publisher.
 *
 * The catalogue is transcription, and transcription is where the errors are.
 * A wrong digit in B_2 moves the index in the fourth decimal place: invisible
 * in any image, and it silently changes the colour correction of every lens
 * built on that glass. So the first test here is not a property test, it is a
 * data audit -- recompute n_d and V_d from the coefficients and compare them
 * to the numbers Schott prints. */
#include "test.h"
#include "tests.h"

#include "opticsim/glass.h"

#include <math.h>
#include <string.h>

void os_test_glass(void) {
    SECTION("glass: the catalogue reproduces its own published constants");
    {
        char why[256];
        bool ok = os_glass_self_check(why, sizeof why);
        if (!ok) NOTE("%s", why);
        CHECK(ok);

        /* And spelled out per glass, so a failure names the culprit rather
         * than just saying the catalogue is bad. */
        for (int id = 0; id < OS_GLASS_COUNT; ++id) {
            const OsGlass *g = os_glass((OsGlassId)id);
            if (g->nd_published <= 0.0) continue;
            CHECK_NEAR(os_glass_n(g, OS_LINE_D), g->nd_published, 1e-4);
            CHECK_NEAR(os_glass_abbe(g), g->vd_published, 1e-3);
        }
    }

    SECTION("glass: air is exactly 1, at every wavelength");
    {
        /* Not approximately 1. The lens tracer compares n across an interface
         * to decide whether to refract at all, and an air index of 1.0000003
         * would make every air-air gap a refracting surface with a tiny,
         * wrong deflection -- which reads as a mysterious softness. */
        const OsGlass *air = os_glass(OS_GLASS_AIR);
        for (ls_real l = 360.0; l <= 830.0; l += 5.0)
            CHECK(os_glass_n(air, l) == 1.0);
        CHECK(os_glass_dispersion(air) == 0.0);
        CHECK(os_glass_abbe(air) == 0.0);   /* 0, not a division by zero */
    }

    SECTION("glass: normal dispersion across the visible band");
    {
        /* Every optical glass has dn/dlambda < 0 in the visible: blue bends
         * more than red. If a table were entered with a sign error somewhere,
         * this is what would catch it, and it is also the physical fact that
         * makes the achromat in the next milestone possible at all. */
        for (int id = 1; id < OS_GLASS_COUNT; ++id) {
            const OsGlass *g = os_glass((OsGlassId)id);
            bool falling = true;
            for (ls_real l = 400.0; l < 700.0; l += 5.0)
                if (os_glass_n(g, l + 5.0) >= os_glass_n(g, l)) falling = false;
            CHECK(falling);

            /* n > 1 everywhere in band, and physically bounded. */
            CHECK(os_glass_n(g, 400.0) > 1.0);
            CHECK(os_glass_n(g, 700.0) < 2.5);

            /* Blue really is slower than red -- the Abbe number's sign. */
            CHECK(os_glass_n(g, OS_LINE_F) > os_glass_n(g, OS_LINE_C));
        }
    }

    SECTION("glass: crowns and flints sit on the right side of the line");
    {
        /* The crown/flint split is the whole basis of achromatic design: pair
         * a low-dispersion crown with a high-dispersion flint. V_d = 50 is the
         * conventional divide. If this ever fails, the achromat derived in the
         * next milestone would be pairing two glasses of the same kind and
         * could not correct colour at all. */
        CHECK(os_glass_abbe(os_glass(OS_GLASS_N_BK7))  > 50.0);
        CHECK(os_glass_abbe(os_glass(OS_GLASS_N_SK16)) > 50.0);
        CHECK(os_glass_abbe(os_glass(OS_GLASS_N_LAK9)) > 50.0);
        CHECK(os_glass_abbe(os_glass(OS_GLASS_F2))     < 50.0);
        CHECK(os_glass_abbe(os_glass(OS_GLASS_SF2))    < 50.0);
        CHECK(os_glass_abbe(os_glass(OS_GLASS_N_SF5))  < 50.0);
        CHECK(os_glass_abbe(os_glass(OS_GLASS_SF11))   < 50.0);

        /* N-BK7 is the reference glass of the whole industry; its numbers are
         * worth pinning as literals so a catalogue-wide edit cannot drift the
         * one glass every other result is anchored to. */
        CHECK_NEAR(os_glass_n(os_glass(OS_GLASS_N_BK7), OS_LINE_D), 1.51680, 1e-5);
        CHECK_NEAR(os_glass_abbe(os_glass(OS_GLASS_N_BK7)), 64.17, 1e-3);
    }

    SECTION("glass: model glass reproduces (n_d, V_d) exactly");
    {
        /* Published prescriptions give only these two numbers per element, so
         * this solve is what lets a real lens table be used faithfully instead
         * of being approximated by the nearest catalogue entry. */
        OsGlass m;
        CHECK(os_glass_model(1.5168, 64.17, &m));
        CHECK_NEAR(os_glass_n(&m, OS_LINE_D), 1.5168, 1e-9);
        CHECK_NEAR(os_glass_abbe(&m), 64.17, 1e-9);

        /* Round trip across the whole realistic (n_d, V_d) box: crowns and
         * flints, high index and low. 1e-9 is the solve's own convergence
         * criterion, so this asserts it really converged rather than stopping. */
        const double nds[] = { 1.45, 1.5168, 1.60, 1.67, 1.72, 1.80, 1.90 };
        const double vds[] = { 20.0, 25.68, 33.85, 45.0, 55.0, 64.17, 75.0 };
        for (int i = 0; i < (int)(sizeof nds / sizeof nds[0]); ++i)
            for (int j = 0; j < (int)(sizeof vds / sizeof vds[0]); ++j) {
                OsGlass g;
                if (!os_glass_model(nds[i], vds[j], &g)) { CHECK(false); continue; }
                CHECK_NEAR(os_glass_n(&g, OS_LINE_D), nds[i], 1e-9);
                CHECK_NEAR(os_glass_abbe(&g), vds[j], 1e-9);
                /* A synthesised glass must still be physical: normal
                 * dispersion, index above 1 across the band. */
                CHECK(os_glass_n(&g, OS_LINE_F) > os_glass_n(&g, OS_LINE_C));
                CHECK(os_glass_n(&g, 400.0) > 1.0);
            }

        /* Nonsense in, refusal out -- never a silently wrong glass. */
        OsGlass bad;
        CHECK(!os_glass_model(0.9, 50.0, &bad));    /* index below 1     */
        CHECK(!os_glass_model(1.5, 0.0, &bad));     /* infinite V_d      */
        CHECK(!os_glass_model(1.5, -10.0, &bad));   /* negative V_d      */
    }

    SECTION("glass: a model glass can stand in for a catalogue one");
    {
        /* The practical claim behind os_glass_model: fitted to a real glass's
         * two published numbers, it tracks that glass across the whole visible
         * band, not just at the three lines it was fitted to. If it did not,
         * a prescription transcribed with model glasses would have the right
         * colour correction and the wrong spectral shape. */
        const OsGlass *bk7 = os_glass(OS_GLASS_N_BK7);
        OsGlass m;
        CHECK(os_glass_model(bk7->nd_published, bk7->vd_published, &m));

        ls_real worst = 0.0;
        for (ls_real l = 420.0; l <= 680.0; l += 10.0) {
            ls_real d = fabs(os_glass_n(&m, l) - os_glass_n(bk7, l));
            if (d > worst) worst = d;
        }
        NOTE("model vs N-BK7, worst |dn| over 420-680 nm: %.2e", worst);
        /* Two free parameters cannot match three-term Sellmeier exactly, but
         * they get within a few units in the fourth decimal -- comfortably
         * better than the catalogue-guessing it replaces. */
        CHECK(worst < 1e-3);
    }
}
