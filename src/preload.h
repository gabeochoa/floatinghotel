#pragma once

#include <afterhours/src/library.h>
#include <afterhours/src/singleton.h>

#include <memory>

#include "external.h"

SINGLETON_FWD(Preload)
struct Preload {
    SINGLETON(Preload)

    Preload();
    ~Preload();

    Preload(const Preload&) = delete;
    void operator=(const Preload&) = delete;

    Preload& init(const char* title);
    Preload& make_singleton();
};

// Switches the live palette and the UI library's widget defaults together.
void apply_ui_theme(bool light);
