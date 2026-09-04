#include "ui/LibraryPanel.h"

#include "Application.h"
#include "ShaderManager.h"
#include "ui/Theme.h"
#include "ui/dialogs/KeybindingDialog.h"
#include "ui/dialogs/NewShaderDialog.h"

#include <QAction>
#include <QBrush>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <string>
#include <vector>

namespace SP {

namespace {

// The preset index lives on the item; the view never carries a second copy of the list.
constexpr int kRolePresetIndex = Qt::UserRole;
constexpr int kNoRow = -2;                 // a group header, or no item at all

// Rows are hit targets in a list of forty-odd names, so they get height the stylesheet's
// padding alone would not give them. The header sits taller again: the three groups are the
// structure of the panel and have to read before any individual name does.
constexpr int kRowHeight   = 28;
constexpr int kGroupHeight = 36;
constexpr int kDotPx       = 10;

// Fill behind the active row. Low enough to sit under text at full contrast, strong enough
// to find at a glance from the other side of the window.
constexpr int kActiveFillAlpha = 38;

enum Group { kGroupAudio = 0, kGroupGenerative, kGroupVideo };
enum Page  { kPageTree = 0, kPageEmpty, kPageNoMatch };

// Only failures get a colour. A green tick beside all forty healthy shaders is decoration
// carrying no information, whereas the failure is the one thing the outgoing library knew
// and never showed: it dropped the shader and left `compileError` unread. Valid presets take
// the transparent dot so the whole column of names keeps one left edge.
QIcon StatusDot(const QColor& colour, const QPaintDevice& device)
{
    const qreal dpr = device.devicePixelRatioF();
    QPixmap pixmap(QSize(static_cast<int>(kDotPx * dpr), static_cast<int>(kDotPx * dpr)));
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    if (colour.alpha() > 0) {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(colour);
        painter.drawEllipse(QRectF(1.0, 1.0, kDotPx - 2.0, kDotPx - 2.0));
    }
    return QIcon(pixmap);
}

// The shortcut sits in the second column, right-aligned and quiet: it identifies the row
// without competing with the name for the eye.
void SetShortcutText(QTreeWidgetItem* item, const QString& combo)
{
    item->setText(1, combo);
    item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    item->setForeground(1, QBrush(Theme::kTextSecondary));

    QFont font = item->font(1);
    font.setPointSize(Theme::kFontSizeCaption);
    item->setFont(1, font);
}

}  // namespace

LibraryPanel::LibraryPanel(Application& app, QWidget* parent)
    : QWidget(parent)
    , m_app(app)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Theme::kSpaceUnit);

    // ---- toolbar -------------------------------------------------------------------------
    auto* tools = new QHBoxLayout;
    tools->setContentsMargins(0, 0, 0, 0);
    tools->setSpacing(Theme::kSpaceUnit);

    auto* scan = new QPushButton(tr("Scan Folder..."), this);
    scan->setToolTip(tr("Load every .hlsl, .fx and .ps file in a folder into the library."));
    connect(scan, &QPushButton::clicked, this, &LibraryPanel::ScanFolder);
    tools->addWidget(scan);

    auto* newShader = new QPushButton(tr("+ New"), this);
    newShader->setToolTip(tr("Write a new shader from the template."));
    connect(newShader, &QPushButton::clicked, this, [this] {
        if (NewShaderDialog::Create(m_app, this).isEmpty()) return;
        Refresh();
        // The new preset is active: MainWindow owes RefreshParameters, OnParamChanged and
        // the editor document, exactly as it does for a row click.
        emit PresetActivated();
    });
    tools->addWidget(newShader);

    tools->addStretch(1);
    layout->addLayout(tools);

    // The filter takes its own row: at dock width it would otherwise be squeezed to a
    // couple of characters beside the buttons.
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Filter by name"));
    m_filter->setClearButtonEnabled(true);
    m_filter->setToolTip(tr("Show only shaders whose name contains this text."));
    connect(m_filter, &QLineEdit::textChanged, this, [this] {
        ApplyFilter();
        MarkActive();   // clearing the filter brings the active row back with the rest
    });
    layout->addWidget(m_filter);

    // ---- the list, and what stands in for it ---------------------------------------------
    m_stack = new QStackedWidget(this);

    m_tree = new QTreeWidget(m_stack);
    m_tree->setColumnCount(2);
    m_tree->setHeaderHidden(true);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->setIndentation(Theme::kSpaceUnit * 2);
    m_tree->setIconSize(QSize(kDotPx, kDotPx));
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setAllColumnsShowFocus(true);
    m_tree->setExpandsOnDoubleClick(false);   // double-click is the keybinding shortcut
    m_tree->setFrameShape(QFrame::NoFrame);   // the stylesheet's border is the frame
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this] {
        const int index = IndexOf(m_tree->currentItem());
        if (index != kNoRow) Activate(index);
    });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
                const int index = IndexOf(item);
                if (index != kNoRow) RequestKeybinding(index);
            });
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &LibraryPanel::ShowContextMenu);

    m_dotError = StatusDot(Theme::kStateError, *this);
    m_dotNone  = StatusDot(QColor(0, 0, 0, 0), *this);

    m_stack->addWidget(m_tree);             // kPageTree
    m_stack->addWidget(MakeEmptyPage());    // kPageEmpty
    m_stack->addWidget(MakeNoMatchPage());  // kPageNoMatch
    layout->addWidget(m_stack, 1);

    Refresh();
}

// A library with nothing in it is a first run, not a failure. It says what to do and gives
// the action that does it, rather than three empty group headings.
QWidget* LibraryPanel::MakeEmptyPage()
{
    auto* page = new QWidget(m_stack);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(Theme::kSpaceUnit, Theme::kSpaceUnit,
                               Theme::kSpaceUnit, Theme::kSpaceUnit);
    layout->setSpacing(0);
    layout->addStretch(2);

    auto* heading = new QLabel(tr("No shaders loaded"), page);
    QFont headingFont(QString::fromLatin1(Theme::kFontUi));
    headingFont.setPointSize(Theme::kFontSizePanelTitle + 3);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    heading->setAlignment(Qt::AlignCenter);
    layout->addWidget(heading);
    layout->addSpacing(Theme::kSpaceUnit);

    auto* blurb = new QLabel(
        tr("Point ShaderPlayer at a folder of .hlsl files and they arrive here, "
           "sorted into audio reactive, generative and video effects."), page);
    blurb->setObjectName(QStringLiteral("Caption"));
    blurb->setAlignment(Qt::AlignCenter);
    blurb->setWordWrap(true);
    layout->addWidget(blurb);
    layout->addSpacing(Theme::kPanelGap);

    auto* scan = new QPushButton(tr("Scan Folder..."), page);
    scan->setObjectName(QStringLiteral("Primary"));
    scan->setMinimumHeight(38);
    scan->setCursor(Qt::PointingHandCursor);
    connect(scan, &QPushButton::clicked, this, &LibraryPanel::ScanFolder);
    layout->addWidget(scan);
    layout->addSpacing(Theme::kSpaceUnit + 2);

    auto* hint = new QLabel(tr("or drop a .hlsl file anywhere on the window"), page);
    hint->setObjectName(QStringLiteral("Caption"));
    hint->setAlignment(Qt::AlignCenter);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    layout->addStretch(3);

    return page;
}

QWidget* LibraryPanel::MakeNoMatchPage()
{
    auto* page = new QWidget(m_stack);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(Theme::kSpaceUnit, Theme::kSpaceUnit,
                               Theme::kSpaceUnit, Theme::kSpaceUnit);
    layout->setSpacing(0);
    layout->addStretch(2);

    m_noMatch = new QLabel(page);
    m_noMatch->setAlignment(Qt::AlignCenter);
    m_noMatch->setWordWrap(true);
    layout->addWidget(m_noMatch);
    layout->addSpacing(Theme::kSpaceUnit + Theme::kSpaceUnit);

    auto* clear = new QPushButton(tr("Clear Filter"), page);
    clear->setCursor(Qt::PointingHandCursor);
    connect(clear, &QPushButton::clicked, this, [this] { m_filter->clear(); });
    layout->addWidget(clear, 0, Qt::AlignHCenter);
    layout->addStretch(3);

    return page;
}

void LibraryPanel::ScanFolder()
{
    m_app.ScanFolderDialog();   // synchronous; A19 replaces it with a Qt dialog
    Refresh();
}

void LibraryPanel::Refresh()
{
    // Everything below moves the selection, and the selection is what activates a preset.
    // Blocking the view for the whole rebuild is what keeps a refresh from coming back
    // round as an activation of whatever row happens to land under the cursor.
    const QSignalBlocker blocker(m_tree);

    m_tree->clear();   // deletes every item, groups included
    m_rows.clear();
    m_passthrough = nullptr;
    for (QTreeWidgetItem*& group : m_groups) group = nullptr;

    // The passthrough row is not a shader: it is the way back to an unprocessed picture, so
    // it sits above the groups and stays out of the filter (filtering away the only route
    // back to no-effect would be a trap).
    m_passthrough = new QTreeWidgetItem(m_tree);
    m_passthrough->setText(0, tr("(No Effect)"));
    m_passthrough->setIcon(0, m_dotNone);
    m_passthrough->setData(0, kRolePresetIndex, -1);
    m_passthrough->setSizeHint(0, QSize(0, kRowHeight));
    m_passthrough->setToolTip(0, tr("Show the source with no shader applied.\n\n"
                                    "Right-click to set a keybinding."));

    const AppConfig& config = m_app.GetConfig();
    if (config.passthroughKey != 0) {
        SetShortcutText(m_passthrough,
                        QString::fromStdString(m_app.GetComboName(
                            config.passthroughKey, config.passthroughModifiers)));
    }

    const QString titles[kGroupCount] = { tr("AUDIO REACTIVE"),
                                          tr("GENERATIVE"),
                                          tr("VIDEO EFFECTS") };
    for (int g = 0; g < kGroupCount; ++g) {
        auto* group = new QTreeWidgetItem(m_tree);
        group->setText(0, titles[g]);
        group->setSizeHint(0, QSize(0, kGroupHeight));
        group->setFlags(Qt::ItemIsEnabled);   // a heading, not a row to land on

        QFont font = group->font(0);
        font.setBold(true);
        font.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
        group->setFont(0, font);

        // The count is the one number worth carrying here, and it is a caption.
        QFont countFont = group->font(1);
        countFont.setPointSize(Theme::kFontSizeCaption);
        group->setFont(1, countFont);
        group->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        group->setForeground(1, QBrush(Theme::kTextSecondary));

        m_groups[g] = group;
    }

    const std::vector<ShaderPreset>& presets = m_app.GetShaderManager().GetPresets();
    m_rows.reserve(static_cast<qsizetype>(presets.size()));

    for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
        const ShaderPreset& preset = presets[static_cast<size_t>(i)];

        // Same classification the rest of the product uses: audio first, then generative,
        // and everything else is a video effect.
        const int group = preset.isAudio      ? kGroupAudio
                        : preset.isGenerative ? kGroupGenerative
                                              : kGroupVideo;

        auto* row = new QTreeWidgetItem(m_groups[group]);
        row->setText(0, preset.name.empty() ? tr("(unnamed)")
                                            : QString::fromStdString(preset.name));
        row->setData(0, kRolePresetIndex, i);
        row->setSizeHint(0, QSize(0, kRowHeight));

        const bool failed = !preset.compileError.empty();
        row->setIcon(0, failed ? m_dotError : m_dotNone);
        row->setToolTip(0, failed
            ? QString::fromStdString(preset.compileError)
            : (preset.filepath.empty()
                   ? tr("Right-click to set a keybinding, or remove it from the library.")
                   : QString::fromStdString(preset.filepath)
                         + tr("\n\nRight-click to set a keybinding, or remove it from "
                              "the library.")));

        if (preset.shortcutKey != 0) {
            SetShortcutText(row, QString::fromStdString(m_app.GetComboName(
                                     preset.shortcutKey, preset.shortcutModifiers)));
        }

        m_rows.append(row);
    }

    m_tree->expandAll();
    ApplyFilter();   // hides what the filter excludes, counts the groups, picks the page
    MarkActive();
}

void LibraryPanel::ApplyFilter()
{
    const QSignalBlocker blocker(m_tree);
    const QString needle = m_filter->text().trimmed();

    int shown = 0;
    for (QTreeWidgetItem* group : m_groups) {
        if (!group) continue;

        int visible = 0;
        for (int c = 0; c < group->childCount(); ++c) {
            QTreeWidgetItem* row = group->child(c);
            const bool match = needle.isEmpty()
                            || row->text(0).contains(needle, Qt::CaseInsensitive);
            row->setHidden(!match);
            if (match) ++visible;
        }

        // An empty group is not a group. It goes, rather than standing there as a heading
        // over nothing.
        group->setHidden(visible == 0);
        group->setText(1, visible > 0 ? QString::number(visible) : QString());
        shown += visible;
    }

    if (m_rows.isEmpty()) {
        m_stack->setCurrentIndex(kPageEmpty);
    } else if (shown == 0) {
        m_noMatch->setText(tr("No shader matches '%1'.").arg(needle));
        m_stack->setCurrentIndex(kPageNoMatch);
    } else {
        m_stack->setCurrentIndex(kPageTree);
    }
}

void LibraryPanel::MarkActive()
{
    const int active = m_app.GetShaderManager().GetActivePresetIndex();

    // Three channels, none of which needs the panel to hold focus: weight, accent colour on
    // the name, and an accent fill across the row.
    const auto mark = [](QTreeWidgetItem* item, bool on) {
        if (!item) return;

        QFont font = item->font(0);
        font.setBold(on);
        item->setFont(0, font);
        item->setForeground(0, QBrush(on ? Theme::kAccentPrimary : Theme::kTextPrimary));

        QColor fill = Theme::kAccentPrimary;
        fill.setAlpha(kActiveFillAlpha);
        const QBrush brush = on ? QBrush(fill) : QBrush();
        item->setBackground(0, brush);
        item->setBackground(1, brush);
    };

    mark(m_passthrough, active < 0);
    for (int i = 0; i < m_rows.size(); ++i) mark(m_rows[i], i == active);

    QTreeWidgetItem* current = (active < 0)
                             ? m_passthrough
                             : (active < m_rows.size() ? m_rows[active] : nullptr);
    if (current && !current->isHidden()) {
        const QSignalBlocker blocker(m_tree);
        m_tree->setCurrentItem(current);
        m_tree->scrollToItem(current);
    }
}

void LibraryPanel::Activate(int presetIndex)
{
    ShaderManager& manager = m_app.GetShaderManager();
    if (presetIndex < 0) {
        manager.SetPassthrough();
    } else {
        manager.SetActivePreset(presetIndex);
    }

    MarkActive();

    // MainWindow's connection owes RefreshParameters() and OnParamChanged() here, which
    // every SetActivePreset call site owes, plus loading the shader into the editor.
    emit PresetActivated();
}

void LibraryPanel::ShowContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    const int index = IndexOf(item);
    if (index == kNoRow) return;

    QMenu menu(this);

    QAction* bind = menu.addAction(tr("Set Keybinding..."));
    connect(bind, &QAction::triggered, this, [this, index] { RequestKeybinding(index); });

    if (index >= 0) {
        menu.addSeparator();
        QAction* remove = menu.addAction(tr("Remove"));
        connect(remove, &QAction::triggered, this, [this, index] {
            ShaderManager& manager = m_app.GetShaderManager();
            // Reversible: this drops the preset from the library and leaves the .hlsl file
            // on disk, so Scan Folder brings it straight back. No confirmation.
            const bool wasActive = (manager.GetActivePresetIndex() == index);
            manager.RemovePreset(index);
            Refresh();
            // Every removal shifts the vector, so the parameters panel and the keyframe
            // tracks under it are left holding a ShaderPreset* into a slot that now belongs
            // to a different preset, or past the end when the active one was last.
            // Removing a preset that was not the active one used to emit nothing at all.
            emit PresetsChanged();
            if (wasActive) emit PresetActivated();
        });
    }

    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void LibraryPanel::RequestKeybinding(int presetIndex)
{
    // The passthrough row is index -1 and stores its binding in AppConfig rather than in a
    // preset, which is the whole difference between the two subjects here.
    const BindingSubject subject = (presetIndex < 0) ? BindingSubject::Passthrough
                                                     : BindingSubject::Shader;
    if (KeybindingDialog::Run(m_app, subject, presetIndex, this)) {
        Refresh();   // the shortcut column is rebuilt from the presets it just changed
    }
}

int LibraryPanel::IndexOf(const QTreeWidgetItem* item) const
{
    if (!item) return kNoRow;
    const QVariant index = item->data(0, kRolePresetIndex);
    return index.isValid() ? index.toInt() : kNoRow;
}

} // namespace SP
