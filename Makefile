# opticsim — physically-based camera and lens simulator
# GNU Make 3.81 compatible (macOS ships 3.81): no .ONESHELL, no $(file ...).

CC      := cc
CSTD    := -std=c11 -D_DEFAULT_SOURCE
WARN    := -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wdouble-promotion \
           -Wstrict-prototypes -Wmissing-prototypes

# -ffp-contract=off keeps FMA contraction from shifting results at the 1e-9
# level, which would make the deterministic optics tests flaky across -O levels:
# the paraxial focal-length checks assert to 1e-9 and the Snell round trip to
# 1e-14. -fno-fast-math is policy, not preference: reassociating the spectral
# sums would break those tolerances, and the estimators rely on NaN/Inf
# semantics to fail loudly rather than to render a plausible wrong picture.
FPFLAGS := -ffp-contract=off -fno-fast-math

# Each configuration builds into its own directory. Sharing one caused
# sanitizer-instrumented objects to be linked into a plain build.
VARIANT ?= release
ifeq ($(VARIANT),release)
  OPT  := -O2 $(FPFLAGS)
  SAN  :=
else ifeq ($(VARIANT),debug)
  OPT  := -O0 -g3 $(FPFLAGS)
  SAN  :=
else ifeq ($(VARIANT),asan)
  OPT  := -O1 -g -fno-omit-frame-pointer $(FPFLAGS)
  SAN  := -fsanitize=address,undefined -fno-sanitize-recover=all
else ifeq ($(VARIANT),ubsan)
  OPT  := -O1 -g -fno-omit-frame-pointer $(FPFLAGS)
  SAN  := -fsanitize=undefined -fno-sanitize-recover=all
endif

# -MMD -MP emits a .d file of header prerequisites next to every object.
# Without this a change to a struct in a header recompiles only the .c files
# that changed, and the rest of the build keeps the OLD struct layout -- which
# does not fail to link, it just reads garbage at runtime.
CFLAGS  := $(CSTD) $(WARN) $(OPT) $(SAN) -Iinclude -MMD -MP
LDLIBS  := -lm -lpthread

BUILD   := build/$(VARIANT)
SRC     := $(wildcard src/*.c)
OBJ     := $(patsubst src/%.c,$(BUILD)/%.o,$(SRC))
TESTSRC := $(wildcard tests/*.c)
TESTOBJ := $(patsubst tests/%.c,$(BUILD)/tests_%.o,$(TESTSRC))
APPSRC  := $(wildcard apps/*.c)
APPOBJ  := $(patsubst apps/%.c,$(BUILD)/apps_%.o,$(APPSRC))

.PHONY: all test check debug clean test-asan test-ubsan docs-images \
        check-exposure-purity check-lens-purity check-sdl-purity \
        check-scale-purity check-photometry-purity check-vendor

# One build command. The viewer is included whenever SDL2 is available; without
# it the library, CLI and tests still build exactly as before.
# ONE BINARY. `opticsim` with no arguments opens the viewer; with a subcommand
# it runs as a batch tool. The viewer half is compiled in when SDL2 is present
# and left out when it is not, so the same command exists either way and says
# what it cannot do rather than failing to build.
SDL_PROBE := $(shell pkg-config --exists sdl2 2>/dev/null && echo yes)
HAVE_VIEWER := $(if $(wildcard viewer/*.c),$(SDL_PROBE),)

SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null)
VIEWSRC    := $(wildcard viewer/*.c)
VIEWOBJ    := $(patsubst viewer/%.c,$(BUILD)/viewer_%.o,$(VIEWSRC))

ifeq ($(HAVE_VIEWER),yes)
  APP_EXTRA_CFLAGS := -DOS_HAVE_SDL -Iviewer
  APP_LINK_OBJ     := $(VIEWOBJ)
  APP_LINK_LIBS    := $(SDL_LIBS)
else
  APP_EXTRA_CFLAGS :=
  APP_LINK_OBJ     :=
  APP_LINK_LIBS    :=
endif

all: opticsim
	@if [ -n "$(wildcard viewer/*.c)" ] && [ "$(SDL_PROBE)" != "yes" ]; then \
	    echo "note: SDL2 not found, so this build has no viewer."; \
	    echo "      brew install sdl2   then re-run make"; \
	fi

opticsim: $(OBJ) $(APPOBJ) $(APP_LINK_OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(APPOBJ) $(APP_LINK_OBJ) $(APP_LINK_LIBS) $(LDLIBS)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/tests_%.o: tests/%.c | $(BUILD)
	$(CC) $(CFLAGS) -Itests -c -o $@ $<

$(BUILD)/apps_%.o: apps/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(APP_EXTRA_CFLAGS) -c -o $@ $<

$(BUILD)/viewer_%.o: viewer/%.c | $(BUILD)
	$(CC) $(CFLAGS) -Iviewer $(SDL_CFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

# These are named explicitly rather than found with $(wildcard): a wildcard is
# expanded when the Makefile is READ, so on a clean tree it matches nothing and
# the tests link without the very modules they are there to exercise -- and
# they pass, because the suites that need them are the ones that vanish.
HEADLESS_VIEW := $(BUILD)/viewer_ui.o $(BUILD)/viewer_font.o \
                 $(BUILD)/viewer_status.o $(BUILD)/viewer_lensplot.o \
                 $(BUILD)/viewer_inspect.o $(BUILD)/viewer_scene3d.o \
                 $(BUILD)/viewer_history.o

$(BUILD)/run_tests: $(OBJ) $(TESTOBJ) $(HEADLESS_VIEW)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(TESTOBJ) $(HEADLESS_VIEW) $(LDLIBS)

check: check-exposure-purity check-lens-purity check-sdl-purity \
       check-scale-purity check-photometry-purity check-vendor

# `all` is a prerequisite so the app binary is never STALE when the tests pass.
# The captures in docs/ come out of ./opticsim and are how a view is checked at
# all; a green test run beside a binary built before the change produces images
# that disagree with the code that just passed, and there is nothing on screen
# to say which one is old. One link is cheap insurance.
test: check all $(BUILD)/run_tests
	$(BUILD)/run_tests

debug:
	$(MAKE) VARIANT=debug all

test-asan:
	$(MAKE) VARIANT=asan test

test-ubsan:
	$(MAKE) VARIANT=ubsan test

# ---- architectural gates --------------------------------------------------
# Four invariants from the design, enforced by the build rather than by
# discipline. Each one guards a bug whose symptom is a picture that looks
# right, which is the only kind of bug worth spending a make target on.

# The whole point of this program is that image brightness comes from the
# f-number, the shutter time and the ISO. One auto-exposure call upstream of
# the display layer makes every exposure demonstration a lie, and it is
# invisible in the output -- the picture just looks correctly exposed.
#
# The gates match CODE, not prose: a line whose first non-space character is
# `*` or `/` is a comment in this codebase's style, and is skipped. Without
# that, explaining in a comment why the auto-exposure branch was removed trips
# the very gate that removal was for -- which teaches people to delete the
# explanation, and the explanation is the point.
NOCOMMENT := grep -vE '^[^:]*:[0-9]+:[[:space:]]*[*/]'

EXPO_SRC := $(filter-out src/os_display.c,$(SRC))
EXPO_PAT := auto_?expos|autoexpose|percentile|normali[sz]e_to_white|p99
check-exposure-purity:
	@hits=""; \
	if [ -n "$(EXPO_SRC)" ]; then \
	    hits=$$(grep -nE '$(EXPO_PAT)' $(EXPO_SRC) 2>/dev/null | $(NOCOMMENT)); \
	fi; \
	if [ -n "$$hits" ]; then \
	    echo "$$hits"; \
	    echo "FAIL: auto-exposure leaked out of the display layer (above)"; exit 1; \
	fi
	@echo "check-exposure-purity: exposure comes from the triangle, nowhere else"

# spectrum.h's non-dispersive invariant holds for SCENE materials. The
# dispersive element here is the LENS, traced sequentially and never through
# Bsdf. A dispersive scene material would silently invalidate the whole
# vendored transport core, so adding one has to be loud.
# os_cli.c is on this list for one reason: `opticsim glass` PRINTS the
# catalogue, and printing an index is not carrying one into the transport core.
# It is the only reader outside the lens layer, and it is listed by name rather
# than by loosening the pattern, so the next one has to be argued for too.
DISP_OK  := src/os_glass.c src/os_glass_data.c src/os_coating.c src/os_lens.c \
            src/os_psf.c src/os_spectral.c src/os_flare.c src/os_pupil.c \
            src/os_prescription_data.c src/os_cli.c
DISP_SRC := $(filter-out $(DISP_OK),$(SRC))
DISP_PAT := sellmeier|os_glass_n|n_at_lambda
check-lens-purity:
	@hits=""; \
	if [ -n "$(DISP_SRC)" ]; then \
	    hits=$$(grep -nE '$(DISP_PAT)' $(DISP_SRC) 2>/dev/null | $(NOCOMMENT)); \
	fi; \
	if [ -n "$$hits" ]; then \
	    echo "$$hits"; \
	    echo "FAIL: dispersion escaped the lens layer (above)"; exit 1; \
	fi
	@echo "check-lens-purity: only the lens knows that n depends on wavelength"

# SDL is a windowing library, not a physics one. Light-Simulation keeps this
# separation by discipline; here the build keeps it, which is what lets the
# toolbar rules and the field semantics be tested with no window.
SDL_ALLOWED := viewer/main.c viewer/draw.c
SDL_GUARDED := $(SRC) $(APPSRC) $(TESTSRC) $(filter-out $(SDL_ALLOWED),$(VIEWSRC)) \
               $(wildcard include/lightsim/*.h include/opticsim/*.h)
check-sdl-purity:
	@if [ -n "$(SDL_GUARDED)" ] && grep -nE '#include *[<"]SDL' $(SDL_GUARDED) 2>/dev/null; then \
	    echo "FAIL: SDL reached outside viewer/main.c and viewer/draw.c (above)"; exit 1; \
	fi
	@echo "check-sdl-purity: the physics and the UI rules are window-free"

# The sensor converts radiometry to electrons using the PHOTOSITE area. If it
# can see the render grid it will eventually use it, and a low-resolution
# preview will look cleaner than the camera it claims to model.
check-scale-purity:
	@if [ -f src/os_sensor.c ] && grep -nE 'render_w|render_h|render_pixel' src/os_sensor.c 2>/dev/null; then \
	    echo "FAIL: os_sensor.c can see the render grid (above)"; exit 1; \
	fi
	@echo "check-scale-purity: electrons are counted per photosite, not per render pixel"

# Lumens enter this program in exactly one place. Every stored quantity is
# radiometric -- watts, W/m^2, W/(m^2 sr) -- and units.c is the only module that
# knows what a lumen is worth. A photometric value landing in an accumulator
# that is later scaled by a radiometric BSDF is off by the luminous efficacy and
# looks merely like a brightness someone should tune.
#
# os_scenedesc.c is listed because it AUTHORS lights: it takes the lumens the
# panel holds and calls ls_watts_from_lumens before anything downstream sees a
# number. That is the seam, and it is named rather than pattern-matched so the
# next file wanting an exemption has to be argued for.
# os_cli.c is here for the same reason it is on the dispersion list: the
# `spectrum` report PRINTS the lumen's definition, and printing a lumen is not
# carrying one into the transport core.
PHOT_OK  := src/units.c src/os_scenedesc.c src/os_cli.c
PHOT_SRC := $(filter-out $(PHOT_OK),$(SRC))
PHOT_PAT := \blumens?\b|\blux\b|\bcandela\b|\b683\b|photometric
check-photometry-purity:
	@hits=""; \
	if [ -n "$(PHOT_SRC)" ]; then \
	    hits=$$(grep -nE '$(PHOT_PAT)' $(PHOT_SRC) 2>/dev/null | $(NOCOMMENT)); \
	fi; \
	if [ -n "$$hits" ]; then \
	    echo "$$hits"; \
	    echo "FAIL: photometry leaked out of the units layer (above)"; exit 1; \
	fi
	@echo "check-photometry-purity: lumens exist only where lights are authored"

# Which files are borrowed and which are ours has to stay answerable at a
# glance. Every vendored file carries a provenance banner on its first line.
VENDORED := $(wildcard include/lightsim/*.h) \
            $(filter-out src/os_%.c,$(SRC))
check-vendor:
	@bad=""; for f in $(VENDORED); do \
	    head -1 "$$f" | grep -q 'VENDORED from Light-Simulation' || bad="$$bad $$f"; \
	done; \
	if [ -n "$$bad" ]; then \
	    echo "FAIL: missing provenance banner:$$bad"; exit 1; \
	fi
	@echo "check-vendor: every borrowed file says where it came from"

# ---- documentation images -------------------------------------------------
# Every README image is produced by the app's own draw path, so a picture
# cannot drift from the code that made it.
docs-images: opticsim
	tools/make-docs-images.sh

-include $(OBJ:.o=.d) $(TESTOBJ:.o=.d) $(APPOBJ:.o=.d) $(VIEWOBJ:.o=.d)

clean:
	# opticsim-view is gone -- there is one binary now -- but it is still
	# removed here so an existing tree does not keep a stale one around that
	# silently runs last week's code.
	rm -rf build opticsim opticsim-view
