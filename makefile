# Detect OS
UNAME_S := $(shell uname -s)

# Compiler settings
ifeq ($(UNAME_S),Darwin)
    CXX := clang++
    EXT := .exe
    MACOS_FLAGS :=
    FRAMEWORKS := -framework CoreFoundation \
        -framework Metal -framework MetalKit -framework Cocoa -framework QuartzCore
else ifeq ($(OS),Windows_NT)
    CXX := g++
    EXT := .exe
    MACOS_FLAGS :=
    FRAMEWORKS :=
else
    CXX := clang++
    EXT :=
    MACOS_FLAGS :=
    FRAMEWORKS :=
endif

# C++ standard
CXXSTD := -std=c++23

# Base compiler flags
CXXFLAGS_BASE := -g \
    -Wall -Wextra -Wpedantic \
    -Wuninitialized -Wshadow -Wconversion \
    -Wcast-qual -Wchar-subscripts \
    -Wcomment -Wdisabled-optimization -Wformat=2 \
    -Wformat-nonliteral -Wformat-security -Wformat-y2k \
    -Wimport -Winit-self -Winline -Winvalid-pch \
    -Wlong-long -Wmissing-format-attribute \
    -Wmissing-include-dirs \
    -Wpacked -Wpointer-arith \
    -Wreturn-type -Wsequence-point \
    -Wstrict-overflow=5 -Wswitch -Wswitch-default \
    -Wswitch-enum -Wtrigraphs \
    -Wunused-label -Wunused-parameter -Wunused-value \
    -Wunused-variable -Wvariadic-macros -Wvolatile-register-var \
    -Wwrite-strings -Warray-bounds \
    -pipe \
    -fno-stack-protector \
    -fno-common

# Warning suppressions
CXXFLAGS_SUPPRESS := -Wno-deprecated-volatile -Wno-missing-field-initializers \
    -Wno-c99-extensions -Wno-unused-function -Wno-sign-conversion \
    -Wno-implicit-int-float-conversion -Wno-implicit-float-conversion \
    -Wno-format-nonliteral -Wno-format-security -Wno-format-y2k \
    -Wno-import -Wno-inline -Wno-invalid-pch \
    -Wno-long-long -Wno-missing-format-attribute \
    -Wno-missing-noreturn -Wno-packed -Wno-redundant-decls \
    -Wno-sequence-point -Wno-trigraphs -Wno-variadic-macros \
    -Wno-volatile-register-var

# Accessibility enforcement (warn and clamp small font sizes)
ACCESSIBILITY_CXXFLAGS := -DAFTERHOURS_ENFORCE_MIN_FONT_SIZE

# Combine all CXXFLAGS
CXXFLAGS := $(CXXSTD) $(CXXFLAGS_BASE) $(CXXFLAGS_SUPPRESS) \
    $(MACOS_FLAGS) $(ACCESSIBILITY_CXXFLAGS) \
    -DAFTER_HOURS_UI_SINGLE_COLLECTION \
    -DAFTER_HOURS_USE_METAL \
    -DFMT_HEADER_ONLY

# Include directories (use -isystem for vendor to suppress their warnings)
INCLUDES := -isystem vendor/ -isystem vendor/afterhours/vendor/

# Library flags
LDFLAGS := -L. -Lvendor/ $(FRAMEWORKS)

# Directories
OBJ_DIR := output/objs
OUTPUT_DIR := output

# Source files (recursive -- includes subdirectories)
MAIN_SRC := $(shell find src -name '*.cpp')

# Objective-C++ source files (for Metal/Sokol)
MAIN_MM_SRC := $(wildcard src/*.mm)
MAIN_MM_OBJS := $(patsubst src/%.mm,$(OBJ_DIR)/main/%.o,$(MAIN_MM_SRC))

# Object files
MAIN_OBJS := $(MAIN_SRC:src/%.cpp=$(OBJ_DIR)/main/%.o)
MAIN_OBJS += $(MAIN_MM_OBJS)
MAIN_OBJS += $(OBJ_DIR)/main/vendor_afterhours_files.o

# Dependency files
MAIN_DEPS := $(MAIN_OBJS:.o=.d)

# Output executable
MAIN_EXE := $(OUTPUT_DIR)/floatinghotel$(EXT)

# Create directories
$(OUTPUT_DIR)/.stamp:
	@mkdir -p $(OUTPUT_DIR)
	@touch $@

$(OBJ_DIR)/main:
	@mkdir -p $(OBJ_DIR)/main

# Default target
.DEFAULT_GOAL := all
all: $(MAIN_EXE)

# Main executable
$(MAIN_EXE): $(MAIN_OBJS) | $(OUTPUT_DIR)/.stamp
	@echo "Linking $(MAIN_EXE)..."
	$(CXX) $(CXXFLAGS) $(MAIN_OBJS) $(LDFLAGS) -o $@
	@echo "Built $(MAIN_EXE)"

# Include dependency files
-include $(MAIN_DEPS)

# Compile main object files
$(OBJ_DIR)/main/%.o: src/%.cpp | $(OBJ_DIR)/main
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@ -MMD -MP -MF $(@:.o=.d) -MT $@

# Compile Objective-C++ files (sokol Metal implementation)
$(OBJ_DIR)/main/%.o: src/%.mm | $(OBJ_DIR)/main
	@echo "Compiling (ObjC++) $<..."
	@mkdir -p $(dir $@)
	$(CXX) -ObjC++ $(CXXFLAGS) $(INCLUDES) -c $< -o $@ -MMD -MP -MF $(@:.o=.d) -MT $@

# Compile afterhours files.cpp
$(OBJ_DIR)/main/vendor_afterhours_files.o: vendor/afterhours/src/plugins/files.cpp | $(OBJ_DIR)/main
	@echo "Compiling vendor/afterhours/src/plugins/files.cpp..."
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@ -MMD -MP -MF $(@:.o=.d) -MT $@

# Force dependency regeneration
deps:
	@echo "Regenerating dependency files..."
	rm -f $(MAIN_DEPS)
	@echo "Dependency files removed - next build will regenerate them"

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(OBJ_DIR)
	@echo "Clean complete"

clean-all: clean
	rm -f $(MAIN_EXE)
	@echo "Cleaned all"

# Resource copying
ifeq ($(UNAME_S),Darwin)
    mkdir_cmd := mkdir -p $(OUTPUT_DIR)/resources/
    cp_resources_cmd := cp -r resources/* $(OUTPUT_DIR)/resources/
else ifeq ($(OS),Windows_NT)
    mkdir_cmd := powershell -command "& {&'New-Item' -Path .\ -Name $(OUTPUT_DIR)\resources -ItemType directory -ErrorAction SilentlyContinue}"
    cp_resources_cmd := powershell -command "& {&'Copy-Item' .\resources\* $(OUTPUT_DIR)\resources -ErrorAction SilentlyContinue}"
else
    mkdir_cmd := mkdir -p $(OUTPUT_DIR)/resources/
    cp_resources_cmd := cp -r resources/* $(OUTPUT_DIR)/resources/
endif

output: $(MAIN_EXE)
	$(mkdir_cmd)
	$(cp_resources_cmd)

run: output
	./$(MAIN_EXE)

install: $(MAIN_EXE)
	@scripts/install.sh

# Utility targets
.PHONY: all clean clean-all deps output run install

# ==============================================================================
# UNIT TESTS
# ==============================================================================

TEST_CXXFLAGS := $(CXXSTD) -g -O0 -Wall -Wextra -Wpedantic \
    -Wno-deprecated-volatile -Wno-missing-field-initializers \
    -Wno-sign-conversion -Wno-implicit-int-float-conversion

TEST_INCLUDES := $(INCLUDES) -I.
TEST_DIR := $(OUTPUT_DIR)/tests

$(TEST_DIR):
	@mkdir -p $(TEST_DIR)

# Individual test executables
$(TEST_DIR)/test_git_parser: tests/unit/test_git_parser.cpp src/git/git_parser.cpp | $(TEST_DIR)
	@echo "Compiling test_git_parser..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $^ -o $@

$(TEST_DIR)/test_error_humanizer: tests/unit/test_error_humanizer.cpp src/git/error_humanizer.cpp | $(TEST_DIR)
	@echo "Compiling test_error_humanizer..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $^ -o $@

$(TEST_DIR)/test_process: tests/unit/test_process.cpp src/util/process.cpp | $(TEST_DIR)
	@echo "Compiling test_process..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $^ -o $@

$(TEST_DIR)/test_settings: tests/unit/test_settings.cpp src/settings.cpp vendor/afterhours/src/plugins/files.cpp | $(TEST_DIR)
	@echo "Compiling test_settings..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $^ -o $@

$(TEST_DIR)/test_git_commands: tests/unit/test_git_commands.cpp src/git/git_commands.cpp src/git/git_runner.cpp src/util/process.cpp | $(TEST_DIR)
	@echo "Compiling test_git_commands..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $^ -o $@

$(TEST_DIR)/test_context_menu: tests/unit/test_context_menu.cpp src/ui/context_menu.cpp | $(TEST_DIR)
	@echo "Compiling test_context_menu..."
	$(CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $^ -o $@

TEST_EXES := $(TEST_DIR)/test_git_parser \
    $(TEST_DIR)/test_error_humanizer \
    $(TEST_DIR)/test_process \
    $(TEST_DIR)/test_settings \
    $(TEST_DIR)/test_git_commands \
    $(TEST_DIR)/test_context_menu

test: $(TEST_EXES)
	@echo "Running unit tests..."
	@PASS=0; FAIL=0; \
	for t in $(TEST_EXES); do \
	    if $$t; then PASS=$$((PASS + 1)); \
	    else FAIL=$$((FAIL + 1)); fi; \
	done; \
	echo "========================================"; \
	echo "Results: $$PASS/$$(( PASS + FAIL )) passed, $$FAIL failed"; \
	echo "========================================"; \
	[ "$$FAIL" -eq 0 ]

.PHONY: test

# ==============================================================================
# VALIDATION
# ==============================================================================

# Run the app briefly against the fixture repo and collect validation warnings.
# Requires GPU access (run from a GUI terminal, not tmux/ssh).
validate: $(MAIN_EXE)
	@echo "Running UI validation..."
	@mkdir -p output/validation
	@if [ ! -d tests/fixture_repo/.git ]; then \
		bash tests/create_fixture_repo.sh; \
	fi
	@$(MAIN_EXE) tests/fixture_repo \
		--test-mode \
		--test-script=tests/e2e_scripts/ui_audit_screenshots.e2e \
		--screenshot-dir=output/validation/screenshots \
		--validation-report=output/validation/report.json \
		--e2e-timeout=10 \
		2>&1 | tee output/validation/validate.log; \
	echo ""; \
	if grep -q "UI Validation Summary" output/validation/validate.log 2>/dev/null; then \
		echo "--- Validation Results ---"; \
		sed -n '/=== UI Validation Summary/,/=== End Validation Summary ===/p' output/validation/validate.log; \
	elif [ -f output/validation/report.json ]; then \
		VCOUNT=$$(grep -c '"message"' output/validation/report.json 2>/dev/null || echo 0); \
		echo "Validation report: $$VCOUNT unique violations"; \
		echo "See output/validation/report.json for details"; \
	else \
		echo "No validation output captured (app may have failed to start)"; \
	fi

.PHONY: validate

# ==============================================================================

# Code counting
count:
	git ls-files | grep "src" | grep -v "resources" | grep -v "vendor" | xargs wc -l | sort -rn | pr -2 -t -w 100

countall:
	git ls-files | xargs wc -l | sort -rn

.PHONY: count countall
