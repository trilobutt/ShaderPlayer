#include "ui/dialogs/KeybindingsReferenceDialog.h"

#include "Application.h"
#include "ShaderManager.h"
#include "WorkspaceManager.h"
#include "ui/Theme.h"

#include <QBrush>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <vector>

namespace SP {

namespace {

constexpr int kDialogWidth  = 520;
constexpr int kTableHeight  = 420;

struct Reserved {
    const char* action;
    const char* binding;
};

// Mirrors Application::FindBindingConflict, which is what actually refuses these keys. A row
// here that is not refused there is a lie, so the two lists are read together.
constexpr Reserved kReserved[] = {
    { "Play / Pause",             "Space"  },
    { "Reset to Passthrough",     "Escape" },
    { "Shader Editor",            "F1"     },
    { "Shader Library",           "F2"     },
    { "Transport Controls",       "F3"     },
    { "Recording Panel",          "F4"     },
    { "Compile Shader",           "F5"     },
    { "Keybindings",              "F6"     },
    { "Video Output Window",      "F7"     },
    { "Spout Output",             "F8"     },
    { "Start / Stop Recording",   "F9"     },
    { "Open Video",               "Ctrl+O" },
    { "Save Shader",              "Ctrl+S" },
    { "New Shader",               "Ctrl+N" },
};

}  // namespace

KeybindingsReferenceDialog::KeybindingsReferenceDialog(Application& app, QWidget* parent)
    : Dialog(tr("Keybindings"), Theme::kAccentPrimary, parent)
    , m_app(app)
{
    setMinimumWidth(kDialogWidth);

    m_table = new QTreeWidget(this);
    m_table->setColumnCount(2);
    m_table->setHeaderLabels({ tr("Action"), tr("Binding") });
    m_table->header()->setStretchLastSection(false);
    m_table->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAllColumnsShowFocus(true);
    m_table->setRootIsDecorated(false);
    m_table->setIndentation(Theme::kSpaceUnit * 2);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setMinimumHeight(kTableHeight);
    Body()->addWidget(m_table, 1);

    FillReserved();
    FillShaders();
    FillWorkspaces();
    m_table->expandAll();

    auto* note = new QLabel(
        tr("Set a shader's shortcut from the library's right-click menu, and a workspace's "
           "from View > Workspace Presets > Manage Workspaces."), this);
    note->setObjectName(QStringLiteral("Caption"));
    note->setWordWrap(true);
    Body()->addWidget(note);

    QHBoxLayout* buttons = AddButtonRow();
    auto* close = new QPushButton(tr("Close"), this);
    close->setObjectName(QStringLiteral("Primary"));
    close->setCursor(Qt::PointingHandCursor);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(close);
    close->setFocus();
}

QTreeWidgetItem* KeybindingsReferenceDialog::AddGroup(const QString& title)
{
    auto* group = new QTreeWidgetItem(m_table);
    group->setText(0, title);
    group->setFirstColumnSpanned(true);
    group->setFlags(Qt::ItemIsEnabled);   // a heading, not a row to select

    QFont font = group->font(0);
    font.setBold(true);
    group->setFont(0, font);
    group->setForeground(0, QBrush(Theme::kAccentPrimary));
    return group;
}

void KeybindingsReferenceDialog::AddRow(QTreeWidgetItem* group, const QString& action,
                                        const QString& binding)
{
    auto* row = new QTreeWidgetItem(group);
    row->setText(0, action);
    row->setText(1, binding);
    row->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    row->setForeground(1, QBrush(Theme::kTextPrimary));
}

void KeybindingsReferenceDialog::AddNote(QTreeWidgetItem* group, const QString& note)
{
    auto* row = new QTreeWidgetItem(group);
    row->setText(0, note);
    row->setFirstColumnSpanned(true);
    row->setFlags(Qt::ItemIsEnabled);
    row->setForeground(0, QBrush(Theme::kTextSecondary));
}

void KeybindingsReferenceDialog::FillReserved()
{
    QTreeWidgetItem* group = AddGroup(tr("Reserved by ShaderPlayer"));
    for (const Reserved& entry : kReserved) {
        AddRow(group, tr(entry.action), QString::fromLatin1(entry.binding));
    }
}

void KeybindingsReferenceDialog::FillShaders()
{
    QTreeWidgetItem* group = AddGroup(tr("Shaders"));

    const AppConfig& config = m_app.GetConfig();
    int bound = 0;

    if (config.passthroughKey != 0) {
        AddRow(group, tr("(No Effect)"),
               QString::fromStdString(m_app.GetComboName(config.passthroughKey,
                                                         config.passthroughModifiers)));
        ++bound;
    }

    const std::vector<ShaderPreset>& presets = m_app.GetShaderManager().GetPresets();
    for (const ShaderPreset& preset : presets) {
        if (preset.shortcutKey == 0) continue;
        AddRow(group, QString::fromStdString(preset.name),
               QString::fromStdString(m_app.GetComboName(preset.shortcutKey,
                                                         preset.shortcutModifiers)));
        ++bound;
    }

    // An empty group with a heading over it reads as broken. This says why it is empty and
    // what fills it.
    if (bound == 0) {
        AddNote(group, presets.empty()
                           ? tr("No shaders are loaded yet.")
                           : tr("No shader has a shortcut. Right-click one in the library "
                                "to give it one."));
    }
}

void KeybindingsReferenceDialog::FillWorkspaces()
{
    QTreeWidgetItem* group = AddGroup(tr("Workspaces"));

    const std::vector<WorkspacePreset>& presets = m_app.GetWorkspaceManager().GetPresets();
    int bound = 0;
    for (const WorkspacePreset& preset : presets) {
        if (preset.shortcutKey == 0) continue;
        AddRow(group, QString::fromStdString(preset.name),
               QString::fromStdString(m_app.GetComboName(preset.shortcutKey,
                                                         preset.shortcutModifiers)));
        ++bound;
    }

    if (bound == 0) {
        AddNote(group, tr("No workspace has a shortcut."));
    }
}

}  // namespace SP
