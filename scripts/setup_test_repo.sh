#!/bin/bash
# Creates a reproducible test git repo at /tmp/floatinghotel_test_repo.
# Uses a cached template for speed — only builds from scratch on first run.
# Uses mv + cp -Rc (APFS clone) instead of rm -rf so the file watcher's
# FSEvents handles on the old directory are harmlessly orphaned rather than
# causing "Directory not empty" failures.
set -euo pipefail

REPO="/tmp/floatinghotel_test_repo"
TEMPLATE="/tmp/floatinghotel_test_template"
TRASH="/tmp/floatinghotel_test_trash_$$"

apply_dirty_state() {
    local dir="$1"
    cd "$dir"

    cat >> README.md << 'DIRTY'

## Development
Run `make` to build.
DIRTY

    sed -i '' 's/Hello, world!/Hello, floatinghotel!/' main.cpp 2>/dev/null || \
    sed -i 's/Hello, world!/Hello, floatinghotel!/' main.cpp

    echo "build/" > .gitignore
    git add .gitignore

    cat > TODO.md << 'TODOEOF'
# TODO
- [ ] Add CI pipeline
- [ ] Write more tests
- [ ] Add error handling
TODOEOF

    printf '\x89PNG\r\n' > icon.png
}

# Fast path: clone from cached template
if [ -d "$TEMPLATE/.git" ]; then
    # Move old repo out of the way (mv is atomic, never fails on locked files)
    if [ -d "$REPO" ]; then
        mv "$REPO" "$TRASH"
    fi
    cp -Rc "$TEMPLATE" "$REPO"
    echo "$REPO"
    exit 0
fi

# Slow path: build template from scratch (only runs once)
if [ -d "$TEMPLATE" ]; then
    find "$TEMPLATE" -delete 2>/dev/null || true
fi
mkdir -p "$TEMPLATE"
cd "$TEMPLATE"

git init -b main >/dev/null 2>&1
git config user.email "test@floatinghotel.dev"
git config user.name "Test User"

# ── Commit 1: initial project ──────────────────────────────
cat > README.md << 'READMEEOF'
# Test Project
A sample project for E2E testing.
READMEEOF

cat > main.cpp << 'CPPEOF'
#include <iostream>

int main() {
    std::cout << "Hello, world!" << std::endl;
    return 0;
}
CPPEOF

cat > utils.h << 'HEOF'
#pragma once
#include <string>

inline std::string greet(const std::string& name) {
    return "Hello, " + name + "!";
}
HEOF

mkdir -p src
cat > src/app.cpp << 'APPEOF'
#include "../utils.h"
#include <iostream>

int main() {
    std::cout << greet("world") << std::endl;
    return 0;
}
APPEOF

git add -A
git commit -m "Initial commit: add project skeleton" >/dev/null 2>&1

# ── Commits 2-53: bulk history for scroll testing ─────────
for i in $(seq 2 53); do
    echo "// update $i" >> src/app.cpp
    git add -A
    git commit -m "Iteration $i: update app logic" >/dev/null 2>&1
done

# ── Commit 54: add tests ──────────────────────────────────
mkdir -p tests
cat > tests/test_utils.cpp << 'TESTEOF'
#include "../utils.h"
#include <cassert>

int main() {
    assert(greet("Alice") == "Hello, Alice!");
    assert(greet("") == "Hello, !");
    return 0;
}
TESTEOF

git add -A
git commit -m "Add unit tests for utils" >/dev/null 2>&1

# ── Commit 55: add docs ───────────────────────────────────
cat > CONTRIBUTING.md << 'CONTRIBEOF'
# Contributing
1. Fork the repo
2. Create a feature branch
3. Submit a PR
CONTRIBEOF

git add -A
git commit -m "Add contributing guidelines" >/dev/null 2>&1

# ── Create feature branch with 1 commit ───────────────────
git checkout -b feature/add-logging >/dev/null 2>&1
cat > src/logger.h << 'LOGEOF'
#pragma once
#include <string>
#include <iostream>

inline void log(const std::string& msg) {
    std::cerr << "[LOG] " << msg << std::endl;
}
LOGEOF

git add -A
git commit -m "feat: add basic logging utility" >/dev/null 2>&1

# ── Back to main ──────────────────────────────────────────
git checkout main >/dev/null 2>&1

# ── Leave working tree dirty (unstaged + untracked) ───────
apply_dirty_state "$TEMPLATE"

# First-time copy from template to repo
cp -Rc "$TEMPLATE" "$REPO"

echo "$REPO"
