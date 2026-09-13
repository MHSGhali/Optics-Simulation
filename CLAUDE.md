# Working on opticsim

A physically-based camera simulator in C. Light is traced through real
multi-element lens prescriptions, surface by surface, at one wavelength at a
time. **Nothing in this program is an effect that gets applied.** Spherical
aberration, coma, astigmatism, field curvature, chromatic aberration,
vignetting, distortion and the shape of the bokeh are all what happens when you
trace real glass, and the tests exist to keep it that way.

That principle is the whole design. Most of the rules below are downstream of
it.

---

## Build and test

```sh
make                 # library + CLI + viewer (viewer only if pkg-config finds sdl2)
make test            # the gates, a fresh build, then the suite   <- the one to run
make test-asan       # the same under -fsanitize=address,undefined
make test-ubsan      # the same under -fsanitize=undefined
make check           # the six architectural gates on their own
make debug           # -O0 -g3 build
make docs-images     # regenerate every image in the README from the app itself
```

**Before you commit, run all four:**

```sh
make test && make test-asan && make test-ubsan && make check
```

`make test` already depends on `check` and on `all` — the binary is a
prerequisite so a green suite can never sit beside a stale `./opticsim`. The
sanitizer runs are not optional: this codebase has shipped a signed-integer
overflow in the RNG stream index that only UBSan could see, and it was silently
corrupting every render above 2.2 megapixels.

Currently **20816 checks**. If your change moves that number down, something was
deleted.

CI runs `make`, `make test`, `make test-asan`, `make test-ubsan` on Linux and
macOS, plus a batch-render smoke test and an offscreen viewer capture.

### A trap in the build

Each `VARIANT` compiles into its own object tree, but they all link to the same
`./opticsim`. So `make VARIANT=ubsan opticsim` leaves a sanitizer binary in
place; run plain `make` afterwards or your next render is instrumented and slow.

---

## The six gates

`make check` enforces by build what would otherwise be enforced by discipline.
Each guards a bug whose symptom is *a picture that looks right*, which is the
only kind worth a make target.

| gate | what it forbids |
|---|---|
| `check-exposure-purity` | auto-exposure anywhere upstream of the display layer. Image brightness comes from the f-number, the shutter and the ISO. One percentile stretch and every exposure demonstration is a lie that looks correct. |
| `check-lens-purity` | dispersion outside the lens layer. `n` depends on wavelength only where the sequential trace can see it; a dispersive *scene* material would silently invalidate the vendored transport core. |
| `check-sdl-purity` | `#include <SDL...>` outside `viewer/main.c` and `viewer/draw.c`. This is what lets the toolbar rules, the field semantics, the lens plot and the 3D view be tested with no window. |
| `check-scale-purity` | `os_sensor.c` seeing the render grid. Electrons are counted per photosite; if the sensor can see render pixels it will eventually use them, and a low-res preview will look cleaner than the camera it models. |
| `check-photometry-purity` | lumens, lux, candela or 683 outside `units.c`, `os_scenedesc.c` and `os_cli.c`. Every stored quantity is radiometric. A photometric value in an accumulator later scaled by a radiometric BSDF is off by the luminous efficacy and merely looks like a brightness someone should tune. |
| `check-vendor` | a borrowed file without its provenance banner on line 1. Which code is ours has to stay answerable at a glance. |

Two things to know before you fight one:

- **The gates match code, not prose.** A line whose first non-space character is
  `*` or `/` is skipped. You are meant to be able to *explain* why the forbidden
  word does not appear — deleting the explanation to appease a grep is the
  failure mode the exemption exists to prevent.
- **Exemptions are named files, never loosened patterns.** `os_cli.c` is on two
  allow-lists because `opticsim glass` and `opticsim spectrum` *print* a
  definition, and printing a lumen is not carrying one into the transport core.
  If you need a new exemption, add the filename and say why — so the next one
  has to be argued for too.

---

## Read the header before you change the module

Every public header opens with **`THE INVARIANT THIS MODULE OWNS`**. These are
not comments, they are the contract, and most of them name the specific bug they
were written after.

| module | owns |
|---|---|
| `glass.h` | every `n` in the program comes from here. No default index, no "about 1.5". |
| `prescription.h` | a prescription is inert data — no f-number, no focus, no chosen focal length. Scaling multiplies *every* length: radii, thicknesses **and** clear apertures. |
| `lens.h` | every ray that reaches the world passed the clear aperture of every surface and the iris, exactly once each. A clipped ray *is* the only vignetting in this program. |
| `pupil.h` | the cached bound always **contains** the true exit pupil. A loose bound costs speed; a tight one silently deletes light from the corners and looks exactly like tasteful vignetting. |
| `camera.h` | the one and only place millimetres meet metres. |
| `spectral.h` | one wavelength per camera sample, positive probability, reciprocal travels with it. Nothing deposits into a bin it did not sample. |
| `render.h` | row y of pass p produces the same numbers at 1 thread and at 64. |
| `scenedesc.h` | an id never changes meaning. Delete leaves a tombstone; `nobj` is a high-water mark, not a count. |
| `stage.h` | every object's distance is known in closed form, so a focus claim is checkable rather than eyeballed. |
| `env.h` | the dome is infinitely far, unoccluded by itself, and **lights the scene without being photographed** — a camera ray that hits nothing returns black. |
| `inspect.h` | exactly one place a setting changes, and **one place its bounds live**. Three copies of a bound is how you get a control that clamps when dragged and not when typed. |

---

## Conventions that are not enforced but are expected

**Comments say why, not what.** The density here is deliberate and high. When
you fix something, the comment records the failure mode — what it looked like,
why it was hard to see. A future reader deciding whether to "simplify" your code
needs the reason, not a restatement of the line below.

**Designs are derived, not guessed.** Prescriptions are built from the
thin-lens design equations at fetch time so their radii carry full double
precision, and the source string says `derived:` with the equations in the
comment above. If you transcribe a real design, say where it came from.
Published tables print only `n_d` per element and never the glass name — and
guessing the glass fixes the index while getting the **Abbe number** wrong,
which is exactly backwards for a program about colour correction.

**Measure, don't assert.** Every number in a test comment was measured. When a
test pins a value, the `NOTE` prints it so a reader can see what the code
actually does. Claims like "this is sharper" belong as a measured ratio.

**Docs images are generated, never screenshotted.** `make docs-images` produces
every picture in the README from the app's own draw path. Anything that changes
the render — including the RNG seeding — means regenerating them, and the result
must be byte-reproducible on a second run.

**Tests state a claim, not a coverage target.** Sections are named for what they
assert ("the ideal lens is ideal in COLOUR, not in its rays"). If a test would
still pass with the feature removed, it is not testing the feature.

---

## Things that have bitten before

- **An element's thickness must accommodate its sag.** Thin-lens derivation
  fixes powers and radii and says nothing about thickness; a biconvex element
  4 mm thick on a 22 mm radius has its faces meeting 12 mm off axis. It cannot
  be built and cannot be traced — the sequential tracer reports every ray
  vignetted and renders *black*. `os_lens_build`'s geometry gate now catches it.
- **The tracer visits surfaces in prescription order, not hit order.** Two
  surfaces that interpenetrate produce a black frame, not an error.
- **Distortion is measured against the paraxial image height at the current
  conjugate.** `f·tan θ` is the *infinite*-conjugate formula, and using it on a
  close-focused lens reports several per cent of the wrong sign.
- **Long object distances wreck sphere-intersection precision.** `dot(m,m) - R²`
  at 10⁹ mm subtracts two numbers near 10¹⁸ to get one near 10⁴. Launch rays
  from near the glass, not from the object.
- **The image circle scales with focal length; the sensor does not.** At 12 mm a
  scaled 100 mm doublet covers 2.4 mm of a 43 mm frame. The corners are honest
  output, not a bug — the `COVERS` row says so.
- **SDL delivers `SDL_KEYDOWN` before `SDL_TEXTINPUT`** for the same press, so
  anything deciding "is this keystroke typing" must decide it in the key
  handler, not from state the text handler sets.

---

## Commits and PRs

Commit messages here are substantive: what changed, and *why it was wrong
before*. Look at the log for the register. Branch off `main`; do not commit onto
a branch whose PR is already merged.

End commit messages with the attribution lines your session specifies, and run
the four commands above before you push.
