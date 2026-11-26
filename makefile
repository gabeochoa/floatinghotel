# Thin wrapper over build.zig so `make`, `make OPT=-O0`, `make test` keep working.
ZIG ?= zig
OPT ?= -O2
MACOS_MIN ?=
ZIG_ARGS ?=
ARGS ?=
ZIG_FLAGS := -Dopt=$(patsubst -O%,%,$(OPT)) $(if $(MACOS_MIN),-Dmacos-min=$(MACOS_MIN)) $(ZIG_ARGS)

.DEFAULT_GOAL := all
.PHONY: all output run test bundle validate install submodules clean clean-all count countall

# The app and the vendored engine move together: a pull that bumps the
# vendor/afterhours pin leaves the old submodule checked out, and the build
# then fails with missing-API errors in app code. Sync a clean submodule to
# the pinned commit automatically (SYNC_SUBMODULES=0 skips this); refuse to
# guess if the submodule has local changes.
SYNC_SUBMODULES ?= 1
submodules:
	@if [ "$(SYNC_SUBMODULES)" != "0" ] && git rev-parse --git-dir >/dev/null 2>&1; then \
		expected=$$(git ls-files -s vendor/afterhours | awk '{print $$2}'); \
		if [ -z "$$expected" ]; then expected=$$(git ls-tree HEAD vendor/afterhours | awk '{print $$3}'); fi; \
		actual=$$(git -C vendor/afterhours rev-parse HEAD 2>/dev/null || echo missing); \
		if [ -n "$$expected" ] && [ "$$expected" != "$$actual" ]; then \
			if [ -n "$$(git -C vendor/afterhours status --porcelain 2>/dev/null)" ]; then \
				echo "error: vendor/afterhours is not at the pinned commit ($$expected) and has local changes; update it manually or run with SYNC_SUBMODULES=0." >&2; \
				exit 1; \
			fi; \
			echo "Updating vendor/afterhours to pinned commit $$expected..."; \
			git submodule update --init --recursive vendor/afterhours; \
		fi; \
	fi

all output: submodules
	$(ZIG) build $(ZIG_FLAGS)
run test: submodules
	$(ZIG) build $@ $(ZIG_FLAGS) $(if $(ARGS),-- $(ARGS))
bundle validate: submodules
	$(ZIG) build $@ $(ZIG_FLAGS)
install: submodules
	$(ZIG) build install-app $(ZIG_FLAGS)
clean:
	rm -rf .zig-cache output/objs
clean-all: clean
	rm -f output/floatinghotel.exe

count:
	git ls-files | grep "src" | grep -v "resources" | grep -v "vendor" | xargs wc -l | sort -rn | pr -2 -t -w 100
countall:
	git ls-files | xargs wc -l | sort -rn
