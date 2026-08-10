#include "ui/dialogs/WorkspacesDialog.h"

#include "Application.h"
#include "WorkspaceManager.h"
#include "ui/Theme.h"
#include "ui/dialogs/KeybindingDialog.h"

#include <QAction>
#include <QBrush>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPoint>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <vector>

namespace SP {

namespace {

constexpr int kDialogWidth = 560;
constexpr int kListHeight  = 300;
constexpr int kRoleIndex   = Qt::UserRole + 1;

}  // namespace

WorkspacesDialog::WorkspacesDialog(Application& app, QWidget* parent)
    : Dialog(tr("Manage Workspaces"), Theme::kAccentPrimary, parent)
    , m_app(app)
{
    setMinimumWidth(kDialogWidth);

    auto* blurb = new QLabel(
        tr("A workspace stores where every panel sits, which ones are open, and an optional "
           "shortcut to bring it back."), this);
    blurb->setObjectName(QStringLiteral("Caption"));
    blurb->setWordWrap(true);
    Body()->addWidget(blurb);

    m_list = new QTreeWidget(this);
    m_list->setColumnCount(2);
    m_list->setHeaderLabels({ tr("Workspace"), tr("Shortcut") });
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_list->setAllColumnsShowFocus(true);
    m_list->setRootIsDecorated(false);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setMinimumHeight(kListHeight);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QTreeWidget::itemSelectionChanged, this, &WorkspacesDialog::SyncActions);
    connect(m_list, &QTreeWidget::customContextMenuRequested,
            this, &WorkspacesDialog::ShowContextMenu);
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem*, int) {
        if (SelectedIndex() > 0) SetKeybinding();
    });
    Body()->addWidget(m_list, 1);

    m_hint = new QLabel(this);
    m_hint->setObjectName(QStringLiteral("Caption"));
    m_hint->setWordWrap(true);
    Body()->addWidget(m_hint);

    QHBoxLayout* buttons = AddButtonRow();

    m_bind = new QPushButton(tr("Set Keybinding..."), this);
    m_bind->setCursor(Qt::PointingHandCursor);
    connect(m_bind, &QPushButton::clicked, this, &WorkspacesDialog::SetKeybinding);
    buttons->addWidget(m_bind);

    m_delete = new QPushButton(tr("Delete"), this);
    m_delete->setObjectName(QStringLiteral("Danger"));
    m_delete->setCursor(Qt::PointingHandCursor);
    connect(m_delete, &QPushButton::clicked, this, &WorkspacesDialog::DeleteSelected);
    buttons->addWidget(m_delete);

    auto* close = new QPushButton(tr("Close"), this);
    close->setObjectName(QStringLiteral("Primary"));
    close->setCursor(Qt::PointingHandCursor);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(close);

    Refresh();
}

void WorkspacesDialog::Refresh()
{
    m_list->clear();

    const std::vector<WorkspacePreset>& presets = m_app.GetWorkspaceManager().GetPresets();
    for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
        const WorkspacePreset& preset = presets[static_cast<size_t>(i)];

        auto* row = new QTreeWidgetItem(m_list);
        row->setData(0, kRoleIndex, i);
        row->setText(0, QString::fromStdString(preset.name));

        if (i == 0) {
            // The built-in cannot be deleted or bound, and says so on the row rather than
            // only by two buttons greying out when it is picked.
            row->setText(0, tr("%1 (built-in)").arg(QString::fromStdString(preset.name)));
            row->setForeground(0, QBrush(Theme::kTextSecondary));
        }

        if (preset.shortcutKey != 0) {
            row->setText(1, QString::fromStdString(
                                m_app.GetComboName(preset.shortcutKey,
                                                   preset.shortcutModifiers)));
        }
        row->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        row->setForeground(1, QBrush(Theme::kTextSecondary));

        QFont font = row->font(1);
        font.setPointSize(Theme::kFontSizeCaption);
        row->setFont(1, font);
    }

    // One row means only the built-in exists, which is a first run rather than a fault.
    const bool onlyBuiltIn = presets.size() <= 1;
    m_hint->setText(onlyBuiltIn
                        ? tr("No workspaces saved yet. Arrange the panels the way you want "
                             "them, then use View > Workspace Presets > Save Current As.")
                        : tr("Deleting a workspace removes its .ini file from the layouts "
                             "folder."));

    SyncActions();
}

int WorkspacesDialog::SelectedIndex() const
{
    const QTreeWidgetItem* row = m_list->currentItem();
    if (!row) return -1;
    const QVariant index = row->data(0, kRoleIndex);
    return index.isValid() ? index.toInt() : -1;
}

void WorkspacesDialog::SyncActions()
{
    // Index 0 is the built-in: WorkspaceManager no-ops both operations on it, so the buttons
    // read as inert rather than silently doing nothing.
    const int index = SelectedIndex();
    const bool editable = index > 0;

    m_bind->setEnabled(editable);
    m_delete->setEnabled(editable);

    const QString why = (index == 0) ? tr("The built-in Default layout cannot be changed.")
                                     : tr("Select a workspace first.");
    m_bind->setToolTip(editable ? tr("Assign a key that brings this layout back.") : why);
    m_delete->setToolTip(editable ? tr("Delete this workspace and its .ini file.") : why);
}

void WorkspacesDialog::SetKeybinding()
{
    const int index = SelectedIndex();
    if (index <= 0) return;

    if (KeybindingDialog::Run(m_app, BindingSubject::Workspace, index, this)) {
        Refresh();
        // The row the user was working on stays selected, so a second change needs no
        // second hunt for it.
        if (index < m_list->topLevelItemCount()) {
            m_list->setCurrentItem(m_list->topLevelItem(index));
        }
    }
}

void WorkspacesDialog::DeleteSelected()
{
    const int index = SelectedIndex();
    if (index <= 0) return;

    m_app.GetWorkspaceManager().DeletePreset(index);
    Refresh();
}

void WorkspacesDialog::ShowContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* row = m_list->itemAt(pos);
    if (!row) return;
    m_list->setCurrentItem(row);
    if (SelectedIndex() <= 0) return;

    QMenu menu(this);

    QAction* bind = menu.addAction(tr("Set Keybinding..."));
    connect(bind, &QAction::triggered, this, &WorkspacesDialog::SetKeybinding);

    menu.addSeparator();

    QAction* remove = menu.addAction(tr("Delete"));
    connect(remove, &QAction::triggered, this, &WorkspacesDialog::DeleteSelected);

    menu.exec(m_list->viewport()->mapToGlobal(pos));
}

}  // namespace SP
