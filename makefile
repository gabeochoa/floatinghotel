# Thin wrapper over build.zig so `make`, `make OPT=-O0`, `make test` keep working.
ZIG ?= zig
OPT ?= -O2
MACOS_MIN ?=
ZIG_ARGS ?=
ARGS ?=
ZIG_FLAGS := -Dopt=$(patsubst -O%,%,$(OPT)) $(if $(MACOS_MIN),-Dmacos-min=$(MACOS_MIN)) $(ZIG_ARGS)

.DEFAULT_GOAL := all
.PHONY: all output run test bundle validate install clean clean-all count countall

all output:
	$(ZIG) build $(ZIG_FLAGS)
run test:
	$(ZIG) build $@ $(ZIG_FLAGS) $(if $(ARGS),-- $(ARGS))
bundle validate:
	$(ZIG) build $@ $(ZIG_FLAGS)
install:
	$(ZIG) build install-app $(ZIG_FLAGS)
clean:
	rm -rf .zig-cache output/objs
clean-all: clean
	rm -f output/floatinghotel.exe

count:
	git ls-files | grep "src" | grep -v "resources" | grep -v "vendor" | xargs wc -l | sort -rn | pr -2 -t -w 100
countall:
	git ls-files | xargs wc -l | sort -rn
