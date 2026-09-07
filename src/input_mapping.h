#pragma once

enum class InputAction {
    None,
    // Required by afterhours UI systems
    WidgetUp,
    WidgetDown,
    WidgetRight,
    WidgetLeft,
    WidgetNext,
    WidgetPress,
    WidgetMod,
    WidgetBack,
    // Required by afterhours text_input (T031). afterhours matches these
    // by name, so a name that is absent silently compiles the feature out --
    // which is how the commit box went without copy/paste/undo.
    TextBackspace,
    TextDelete,
    TextHome,
    TextEnd,
    TextSelectAll,
    TextCopy,
    TextCut,
    TextPaste,
    TextUndo,
    TextRedo,
    TextSelectLeft,
    TextSelectRight,
    TextWordLeft,
    TextWordRight,
    TextDeleteWordBack,
    TextDeleteWordForward,
    MenuBack,
    // Will be extended by T040 (keyboard navigation)
};
