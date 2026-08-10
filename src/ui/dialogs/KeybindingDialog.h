#pragma once

// KeybindingDialog.h — assign, change or clear one keyboard binding.
//
// One dialog serves all three things the product binds: a shader preset, the passthrough
// (No Effect) slot, and a workspace preset. They differ only in where the binding is stored
// and in which index Application::FindBindingConflict is told to ignore, so a subject enum
// and an index is the whole difference between them.
//
// Two properties are load-bearing:
//
//   Nothing typed here reaches the application. The dialog grabs the keyboard while it is
//   open, so every press arrives at keyPressEvent and is consumed there. Without the grab a
//   focused button would still swallow Space and a Tab would still move focus, which is
//   exactly the set of keys a user is here to bind.
//
//   Qt key codes are not Win32 VK codes. Every binding in the product is stored, compared
//   and dispatched as a VK code, so a captured QKeyEvent is mapped back before it is stored
//   or handed to FindBindingConflict.

#include "ui/dialogs/Dialog.h"

#include <QString>

QT_BEGIN_NAMESPACE
class QKeyEvent;
class QLabel;
class QPushButton;
QT_END_NAMESPACE

namespace SP {

class Application;

enum class BindingSubject {
    Shader,        // index is a ShaderManager preset index
    Passthrough,   // index is unused; the binding lives in AppConfig
    Workspace,     // index is a WorkspaceManager preset index
};

class KeybindingDialog : public Dialog {
    Q_OBJECT
public:
    KeybindingDialog(Application& app, BindingSubject subject, int index,
                     QWidget* parent = nullptr);

    // Constructs, runs and destroys a dialog. Returns true when a binding was committed or
    // cleared, which is the caller's cue to refresh whatever displays it. Constructed per
    // showing on purpose: a cached instance would carry the previous session's captured key
    // and conflict message back onto the screen.
    static bool Run(Application& app, BindingSubject subject, int index,
                    QWidget* parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    QString SubjectName() const;
    QString CurrentBinding() const;   // what the subject is bound to right now

    void Capture(int vk, int mods);   // vk == 0 clears
    void Commit();
    void Refresh();                   // preview, conflict line and the OK button

    Application& m_app;
    BindingSubject m_subject;
    int m_index = -1;

    // The pending binding: nothing captured yet until m_captured, then m_vk == 0 means the
    // user asked for the binding to be cleared.
    bool m_captured = false;
    int m_vk = 0;
    int m_mods = 0;

    QString m_conflict;   // empty when the pending binding is free
    QString m_failure;    // a write that did not land (workspace .ini files)

    QLabel* m_subjectLine = nullptr;
    QLabel* m_preview = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_ok = nullptr;
};

}  // namespace SP
