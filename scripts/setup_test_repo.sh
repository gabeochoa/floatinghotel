#!/bin/bash
# Creates a reproducible test git repo at /tmp/floatinghotel_test_repo.
# Uses a cached template for speed — only builds from scratch on first run.
set -euo pipefail

REPO="/tmp/floatinghotel_test_repo"
TEMPLATE="/tmp/floatinghotel_test_template"

# Fast path: copy from cached template
if [ -d "$TEMPLATE/.git" ]; then
    rm -rf "$REPO"
    cp -R "$TEMPLATE" "$REPO"
    # Re-apply the dirty working tree state (cp preserves it, but
    # git index needs to be valid — just verify)
    echo "$REPO"
    exit 0
fi

# Slow path: build template from scratch (only runs once)
rm -rf "$TEMPLATE"
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

# Modify an existing tracked file (unstaged change)
cat >> README.md << 'DIRTY'

## Development
Run `make` to build.
DIRTY

# Modify another tracked file
sed -i '' 's/Hello, world!/Hello, floatinghotel!/' main.cpp 2>/dev/null || \
sed -i 's/Hello, world!/Hello, floatinghotel!/' main.cpp

# Stage one file (so we have both staged and unstaged)
echo "build/" > .gitignore
git add .gitignore

# Create an untracked file
cat > TODO.md << 'TODOEOF'
# TODO
- [ ] Add CI pipeline
- [ ] Write more tests
- [ ] Add error handling
TODOEOF

# Create a binary-ish file (untracked)
printf '\x89PNG\r\n' > icon.png

# Now copy template to actual repo
rm -rf "$REPO"
cp -R "$TEMPLATE" "$REPO"

echo "$REPO"
