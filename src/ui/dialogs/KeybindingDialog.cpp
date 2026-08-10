#include "ui/dialogs/KeybindingDialog.h"

#include "Application.h"
#include "ShaderManager.h"
#include "WorkspaceManager.h"
#include "ui/KeyMap.h"
#include "ui/Theme.h"

#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QStyle>
#include <QVBoxLayout>

#include <vector>

namespace SP {

namespace {

constexpr int kDialogWidth   = 440;
constexpr int kPreviewHeight = 64;
constexpr int kPreviewPoints = 17;   // pt; the one thing on this surface being read
constexpr int kStatusLines   = 2;    // reserved, so a conflict does not resize the dialog

// Qt-to-VK mapping lives in ui/KeyMap.h — shared with MainWindow::keyPressEvent, which
// fires the bindings this dialog captures. A second copy here would let the two drift.

bool IsModifierKey(int key)
{
    return key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt
        || key == Qt::Key_AltGr   || key == Qt::Key_Meta;
}

}  // namespace

KeybindingDialog::KeybindingDialog(Application& app, BindingSubject subject, int index,
                                   QWidget* parent)
    : Dialog(tr("Set Keybinding"), Theme::kAccentPrimary, parent)
    , m_app(app)
    , m_subject(subject)
    , m_index(index)
{
    setMinimumWidth(kDialogWidth);

    m_subjectLine = new QLabel(this);
    m_subjectLine->setWordWrap(true);
    m_subjectLine->setText(SubjectName());
    Body()->addWidget(m_subjectLine);

    auto* instruction = new QLabel(
        tr("Hold any modifiers and press the key. Letters, digits and F1 to F12 can be "
           "bound. Delete clears the binding, Escape cancels."), this);
    instruction->setObjectName(QStringLiteral("Caption"));
    instruction->setWordWrap(true);
    Body()->addWidget(instruction);

    // The capture field: recessed, because it is a field being filled rather than a surface
    // sitting on the dialog, and large, because the combination is the one thing on this
    // screen the user is reading.
    m_preview = new QLabel(this);
    m_preview->setObjectName(QStringLiteral("KeybindingPreview"));
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumHeight(kPreviewHeight);
    QFont previewFont(QString::fromLatin1(Theme::kFontUi));
    previewFont.setPointSize(kPreviewPoints);
    previewFont.setBold(true);
    m_preview->setFont(previewFont);
    Body()->addWidget(m_preview);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setMinimumHeight(m_status->fontMetrics().height() * kStatusLines);
    m_status->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    Body()->addWidget(m_status);

    QHBoxLayout* buttons = AddButtonRow();

    auto* clear = new QPushButton(tr("Clear"), this);
    clear->setCursor(Qt::PointingHandCursor);
    clear->setToolTip(tr("Leave this action with no keyboard shortcut."));
    connect(clear, &QPushButton::clicked, this, [this] { Capture(0, 0); });
    buttons->addWidget(clear);

    auto* cancel = new QPushButton(tr("Cancel"), this);
    cancel->setCursor(Qt::PointingHandCursor);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(cancel);

    m_ok = new QPushButton(tr("Assign"), this);
    m_ok->setObjectName(QStringLiteral("Primary"));
    m_ok->setCursor(Qt::PointingHandCursor);
    connect(m_ok, &QPushButton::clicked, this, &KeybindingDialog::Commit);
    buttons->addWidget(m_ok);

    Refresh();
}

bool KeybindingDialog::Run(Application& app, BindingSubject subject, int index,
                           QWidget* parent)
{
    KeybindingDialog dialog(app, subject, index, parent);
    return dialog.exec() == QDialog::Accepted;
}

// ---------------------------------------------------------------------------------------
// Subject
// ---------------------------------------------------------------------------------------

QString KeybindingDialog::SubjectName() const
{
    switch (m_subject) {
    case BindingSubject::Passthrough:
        return tr("Shader: (No Effect)");
    case BindingSubject::Shader: {
        const std::vector<ShaderPreset>& presets = m_app.GetShaderManager().GetPresets();
        if (m_index < 0 || m_index >= static_cast<int>(presets.size())) return tr("Shader");
        return tr("Shader: %1")
            .arg(QString::fromStdString(presets[static_cast<size_t>(m_index)].name));
    }
    case BindingSubject::Workspace: {
        const std::vector<WorkspacePreset>& presets =
            m_app.GetWorkspaceManager().GetPresets();
        if (m_index < 0 || m_index >= static_cast<int>(presets.size())) return tr("Workspace");
        return tr("Workspace: %1")
            .arg(QString::fromStdString(presets[static_cast<size_t>(m_index)].name));
    }
    }
    return QString();
}

QString KeybindingDialog::CurrentBinding() const
{
    int vk = 0;
    int mods = 0;
    switch (m_subject) {
    case BindingSubject::Passthrough:
        vk   = m_app.GetConfig().passthroughKey;
        mods = m_app.GetConfig().passthroughModifiers;
        break;
    case BindingSubject::Shader: {
        const std::vector<ShaderPreset>& presets = m_app.GetShaderManager().GetPresets();
        if (m_index < 0 || m_index >= static_cast<int>(presets.size())) break;
        vk   = presets[static_cast<size_t>(m_index)].shortcutKey;
        mods = presets[static_cast<size_t>(m_index)].shortcutModifiers;
        break;
    }
    case BindingSubject::Workspace: {
        const std::vector<WorkspacePreset>& presets =
            m_app.GetWorkspaceManager().GetPresets();
        if (m_index < 0 || m_index >= static_cast<int>(presets.size())) break;
        vk   = presets[static_cast<size_t>(m_index)].shortcutKey;
        mods = presets[static_cast<size_t>(m_index)].shortcutModifiers;
        break;
    }
    }
    if (vk == 0) return QString();
    return QString::fromStdString(m_app.GetComboName(vk, mods));
}

// ---------------------------------------------------------------------------------------
// Capture
// ---------------------------------------------------------------------------------------

void KeybindingDialog::showEvent(QShowEvent* event)
{
    Dialog::showEvent(event);

    // Every key press in the product is a potential binding, including the ones Qt would
    // otherwise spend on the focused button (Space, Return) or on focus traversal (Tab).
    // The grab routes all of them here, so nothing typed while this dialog is open can act
    // on the application underneath it.
    grabKeyboard();
}

void KeybindingDialog::hideEvent(QHideEvent* event)
{
    releaseKeyboard();
    Dialog::hideEvent(event);
}

void KeybindingDialog::keyPressEvent(QKeyEvent* event)
{
    event->accept();   // consumed here in every branch; nothing propagates

    if (event->isAutoRepeat()) return;

    const int key = event->key();

    // Modifiers alone are not a binding, but they are half of one: showing them as they go
    // down is what makes the field feel like it is listening.
    if (IsModifierKey(key)) {
        Refresh();
        return;
    }

    switch (key) {
    case Qt::Key_Escape:
        reject();
        return;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        Capture(0, 0);
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (m_ok->isEnabled()) Commit();
        return;
    default:
        break;
    }

    const int vk = VkFromQt(*event);
    if (vk == 0) {
        m_captured = false;
        m_vk = 0;
        m_mods = 0;
        m_conflict.clear();
        m_failure = tr("That key cannot be bound. Use a letter, a digit or F1 to F12, on "
                       "the main keyboard rather than the keypad.");
        Refresh();
        return;
    }

    Capture(vk, ModsFromQt(event->modifiers()));
}

void KeybindingDialog::keyReleaseEvent(QKeyEvent* event)
{
    event->accept();
    if (event->isAutoRepeat()) return;

    // A modifier lifted before the trigger key changes what is being previewed, so the field
    // follows it back down.
    if (IsModifierKey(event->key()) && !m_captured) Refresh();
}

void KeybindingDialog::Capture(int vk, int mods)
{
    m_captured = true;
    m_vk = vk;
    m_mods = mods;
    m_failure.clear();

    // The conflict is computed the instant the key lands, not on commit: a message that only
    // appears after pressing Assign is a message the user has already stopped looking for.
    // The subject's own binding is excluded so re-pressing what it is already bound to is
    // not reported as a conflict with itself.
    if (vk == 0) {
        m_conflict.clear();
    } else {
        switch (m_subject) {
        case BindingSubject::Shader:
            m_conflict = QString::fromStdString(
                m_app.FindBindingConflict(vk, mods, m_index, -1));
            break;
        case BindingSubject::Passthrough:
            m_conflict = QString::fromStdString(
                m_app.FindBindingConflict(vk, mods, -1, -1, /*excludePassthrough=*/true));
            break;
        case BindingSubject::Workspace:
            m_conflict = QString::fromStdString(
                m_app.FindBindingConflict(vk, mods, -1, m_index));
            break;
        }
    }

    Refresh();
}

void KeybindingDialog::Refresh()
{
    // ---- the field ---------------------------------------------------------------------
    QString text;
    QColor ink = Theme::kTextPrimary;

    if (m_captured && m_vk == 0) {
        text = tr("No binding");
        ink = Theme::kTextSecondary;
    } else if (m_captured) {
        text = QString::fromStdString(m_app.GetComboName(m_vk, m_mods));
        ink = m_conflict.isEmpty() ? Theme::kAccentPrimary : Theme::kStateError;
    } else {
        // Nothing captured yet: show the modifiers already held, so the field is visibly
        // tracking the keyboard before the trigger key arrives.
        const int held = ModsFromQt(QGuiApplication::keyboardModifiers());
        if (held != 0) {
            QString parts;
            if (held & MOD_CONTROL) parts += QStringLiteral("Ctrl+");
            if (held & MOD_ALT)     parts += QStringLiteral("Alt+");
            if (held & MOD_SHIFT)   parts += QStringLiteral("Shift+");
            text = parts + QStringLiteral("...");
            ink = Theme::kTextPrimary;
        } else {
            text = tr("Press a key");
            ink = Theme::kTextSecondary;
        }
    }

    m_preview->setText(text);
    m_preview->setStyleSheet(
        QStringLiteral("QLabel#KeybindingPreview { background: %1; border: 1px solid %2;"
                       " border-radius: %3px; color: %4; padding: 4px 10px; }")
            .arg(Rgba(Theme::kInputFill),
                 Rgba(m_captured && !m_conflict.isEmpty() ? Theme::kStateError
                                                          : Theme::kPanelBorder))
            .arg(Theme::kRadiusControl)
            .arg(ink.name()));

    // ---- the line underneath -------------------------------------------------------------
    if (!m_failure.isEmpty()) {
        m_status->setObjectName(QStringLiteral("StatusError"));
        m_status->setText(m_failure);
    } else if (!m_conflict.isEmpty()) {
        m_status->setObjectName(QStringLiteral("StatusError"));
        m_status->setText(tr("Already %1. Choose a different key.").arg(m_conflict));
    } else if (m_captured && m_vk == 0) {
        m_status->setObjectName(QStringLiteral("Caption"));
        m_status->setText(tr("Assign will remove the existing shortcut."));
    } else if (m_captured) {
        m_status->setObjectName(QStringLiteral("StatusOk"));
        m_status->setText(tr("Free. Assign to use it."));
    } else {
        m_status->setObjectName(QStringLiteral("Caption"));
        const QString current = CurrentBinding();
        m_status->setText(current.isEmpty()
                              ? tr("Currently unbound.")
                              : tr("Currently bound to %1.").arg(current));
    }
    // The object name selects which stylesheet rule applies, and Qt only re-resolves that on
    // an explicit repolish.
    m_status->style()->unpolish(m_status);
    m_status->style()->polish(m_status);

    // ---- the button ----------------------------------------------------------------------
    // Visibly refuses rather than silently doing nothing: the disabled state is the answer
    // to "why is nothing happening", and the line above says what to do about it.
    const bool commitable = m_captured && m_conflict.isEmpty();
    m_ok->setEnabled(commitable);
    m_ok->setToolTip(commitable
                         ? tr("Store this shortcut.")
                         : (m_conflict.isEmpty() ? tr("Press a key combination first.")
                                                 : tr("That combination is already taken.")));
}

// ---------------------------------------------------------------------------------------
// Commit
// ---------------------------------------------------------------------------------------

void KeybindingDialog::Commit()
{
    if (!m_captured || !m_conflict.isEmpty()) return;

    switch (m_subject) {
    case BindingSubject::Passthrough: {
        AppConfig& config = m_app.GetConfig();
        config.passthroughKey = m_vk;
        config.passthroughModifiers = m_mods;
        m_app.SaveConfig();
        break;
    }
    case BindingSubject::Shader: {
        ShaderPreset* preset = m_app.GetShaderManager().GetPreset(m_index);
        if (!preset) {
            // The preset went away while the dialog was open (removed from the library).
            reject();
            return;
        }
        preset->shortcutKey = m_vk;
        preset->shortcutModifiers = m_mods;
        m_app.SaveConfig();
        break;
    }
    case BindingSubject::Workspace:
        // The binding lives in the preset's .ini header, so this one can genuinely fail.
        // A failed write keeps the dialog open and says so; closing on it would report a
        // shortcut that is not on disk.
        if (!m_app.GetWorkspaceManager().SetKeybinding(m_index, m_vk, m_mods)) {
            m_failure = tr("The layout file could not be written, so the shortcut was not "
                           "saved.");
            Refresh();
            return;
        }
        break;
    }

    accept();
}

}  // namespace SP
