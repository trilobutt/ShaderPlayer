#pragma once

// WorkspacesDialog.h — manage the saved window layouts: their shortcuts, and deleting them.
//
// Loading a layout is not here. That belongs to the View > Workspace Presets menu, which
// lists them and applies one; this dialog is where a layout is given a shortcut or thrown
// away, which are the two things a menu item cannot do.
//
// Every row's actions are on visible buttons as well as in the right-click menu. The
// outgoing modal offered them only on right-click, which left the built-in Default row
// looking identical to a deletable one and the two real actions undiscoverable.

#include "ui/dialogs/Dialog.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QPoint;
class QPushButton;
class QTreeWidget;
QT_END_NAMESPACE

namespace SP {

class Application;

class WorkspacesDialog : public Dialog {
    Q_OBJECT
public:
    explicit WorkspacesDialog(Application& app, QWidget* parent = nullptr);

private:
    void Refresh();                     // rebuild the rows from WorkspaceManager
    void SyncActions();                 // enable the two row actions for the selection
    int SelectedIndex() const;          // -1 when nothing usable is selected

    void SetKeybinding();
    void DeleteSelected();
    void ShowContextMenu(const QPoint& pos);

    Application& m_app;

    QTreeWidget* m_list = nullptr;
    QLabel* m_hint = nullptr;
    QPushButton* m_bind = nullptr;
    QPushButton* m_delete = nullptr;
};

}  // namespace SP
