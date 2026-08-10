#pragma once

// KeybindingsReferenceDialog.h — every keyboard binding in the product, in one read-only
// list: the ones the application reserves and the ones the user has assigned.
//
// This was a dock in the outgoing UI, which cost a permanent slot in the layout for a
// reference a user reads twice. It is a dialog now (View > Keybindings), and it does not
// edit: a shader binding is set from the library's right-click menu and a workspace binding
// from Manage Workspaces, both of which show the same KeybindingDialog.

#include "ui/dialogs/Dialog.h"

QT_BEGIN_NAMESPACE
class QTreeWidget;
class QTreeWidgetItem;
QT_END_NAMESPACE

namespace SP {

class Application;

class KeybindingsReferenceDialog : public Dialog {
    Q_OBJECT
public:
    explicit KeybindingsReferenceDialog(Application& app, QWidget* parent = nullptr);

private:
    QTreeWidgetItem* AddGroup(const QString& title);
    void AddRow(QTreeWidgetItem* group, const QString& action, const QString& binding);
    void AddNote(QTreeWidgetItem* group, const QString& note);

    void FillReserved();
    void FillShaders();
    void FillWorkspaces();

    Application& m_app;
    QTreeWidget* m_table = nullptr;
};

}  // namespace SP
