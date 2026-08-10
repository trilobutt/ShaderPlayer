#pragma once

// NewShaderDialog.h — name a new shader and put it in the library, on the template.
//
// The dialog collects a name; Create() is what actually builds the preset, because the same
// four ShaderManager calls are owed by both entry points (the Shader menu and the library's
// "+ New" button) and two copies of them is two things to keep in step.
//
// What Create() does NOT do is refresh the window. Its caller owns that: LibraryPanel emits
// PresetActivated and MainWindow calls RefreshAll, and both of those routes already carry
// the follow-ups every SetActivePreset call site owes (see CLAUDE.md).

#include "ui/dialogs/Dialog.h"

#include <QString>

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QPushButton;
QT_END_NAMESPACE

namespace SP {

class Application;

class NewShaderDialog : public Dialog {
    Q_OBJECT
public:
    explicit NewShaderDialog(QWidget* parent = nullptr);

    QString Name() const;

    // Runs the dialog and, if it was accepted, creates the shader and makes it active.
    // Returns the new preset's name, or an empty string when the user cancelled.
    // Constructed per showing, so nothing from the previous run survives into this one.
    static QString Create(Application& app, QWidget* parent = nullptr);

private:
    void SyncCreateButton();

    QLineEdit* m_name = nullptr;
    QPushButton* m_create = nullptr;
};

}  // namespace SP
