#pragma once

// LibraryPanel.h — the body of the Shader Library dock.
//
// A QTreeWidget over ShaderManager's preset vector, in the three fixed groups the product
// classifies by (AUDIO REACTIVE, GENERATIVE, VIDEO EFFECTS), with the passthrough row above
// them. Forty-odd names is a list the user scans rather than reads, so the group headers
// carry real weight, the rows are spaced to be hit rather than squeezed, and the active
// preset is marked in three channels at once (weight, accent colour, accent fill) because a
// selection highlight alone disappears the moment the panel loses focus.
//
// The panel rebuilds only from MainWindow::RefreshLibrary. Traffic the other way is the
// PresetActivated() signal: the two follow-ups every SetActivePreset call site owes
// (MainWindow::RefreshParameters and Application::OnParamChanged, see CLAUDE.md) and the
// editor document all belong to MainWindow, and its connection performs all three.

#include <QIcon>
#include <QVector>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
QT_END_NAMESPACE

namespace SP {

class Application;

class LibraryPanel : public QWidget {
    Q_OBJECT
public:
    explicit LibraryPanel(Application& app, QWidget* parent = nullptr);

    // Rebuilds the tree from ShaderManager and re-marks the active preset. Every mutation
    // of the view runs under a signal block, so a refresh triggered from inside this panel
    // cannot come back round as another activation.
    void Refresh();

signals:
    // A row changed the active preset (a preset row, the passthrough row, or a removal that
    // took the active preset with it).
    void PresetActivated();

private:
    static constexpr int kGroupCount = 3;   // audio, generative, video effects

    QWidget* MakeEmptyPage();
    QWidget* MakeNoMatchPage();

    void ScanFolder();
    void ApplyFilter();
    void MarkActive();
    void Activate(int presetIndex);        // -1 = passthrough
    void ShowContextMenu(const QPoint& pos);
    void RequestKeybinding(int presetIndex);

    // The preset index carried by a row: -1 for passthrough, kNoRow for a group header.
    int IndexOf(const QTreeWidgetItem* item) const;

    Application& m_app;

    QTreeWidget* m_tree = nullptr;
    QLineEdit* m_filter = nullptr;
    QStackedWidget* m_stack = nullptr;
    QLabel* m_noMatch = nullptr;

    QIcon m_dotError;   // a preset that failed to compile
    QIcon m_dotNone;    // transparent, so every name keeps the same left edge

    QTreeWidgetItem* m_passthrough = nullptr;
    QTreeWidgetItem* m_groups[kGroupCount] = {};
    QVector<QTreeWidgetItem*> m_rows;   // parallel to ShaderManager's preset vector
};

} // namespace SP
