#pragma once

// KeyMap.h — the one Qt-key-to-Win32-VK mapping in the product.
//
// Every binding is stored, compared and dispatched as a VK code (ShaderPreset::
// shortcutKey, AppConfig::passthroughKey, WorkspacePreset::shortcutKey, the dispatch in
// Application::HandleKeyboardShortcuts and every check in FindBindingConflict), while Qt
// delivers Qt::Key values. Both ends of that — KeybindingDialog capturing a binding and
// MainWindow::keyPressEvent firing one — go through here, because two mappings that
// disagree is exactly how a shortcut ends up bindable but not firable.
//
// Coverage is A-Z, 0-9 and F1-F12: the set the binding dialog accepts and the only set
// Application::GetKeyName can name. Anything else returns 0. The keypad is refused
// deliberately — Windows sends VK_NUMPAD0-9 for those keys, so a binding stored as '0'
// would be one that never fires.
//
// Space and Escape are absent on purpose. They are reserved actions rather than bindable
// ones (FindBindingConflict refuses both), so the dialog must keep refusing them; the
// dispatch side maps them itself where it needs them.

#include "Common.h"

#include <QKeyEvent>

namespace SP {

inline int VkFromQt(const QKeyEvent& event)
{
    if (event.modifiers().testFlag(Qt::KeypadModifier)) return 0;

    const int key = event.key();
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return 'A' + (key - Qt::Key_A);
    if (key >= Qt::Key_0 && key <= Qt::Key_9) return '0' + (key - Qt::Key_0);
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) return VK_F1 + (key - Qt::Key_F1);
    return 0;
}

inline int ModsFromQt(Qt::KeyboardModifiers modifiers)
{
    int mods = 0;
    if (modifiers.testFlag(Qt::ControlModifier)) mods |= MOD_CONTROL;
    if (modifiers.testFlag(Qt::AltModifier))     mods |= MOD_ALT;
    if (modifiers.testFlag(Qt::ShiftModifier))   mods |= MOD_SHIFT;
    return mods;
}

}  // namespace SP
