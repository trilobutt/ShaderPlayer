#include "ui/dialogs/NewShaderDialog.h"

#include "Application.h"
#include "ShaderManager.h"
#include "ui/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace SP {

namespace {
constexpr int kDialogWidth = 440;
}  // namespace

NewShaderDialog::NewShaderDialog(QWidget* parent)
    : Dialog(tr("New Shader"), Theme::kRegionLibrary, parent)
{
    setMinimumWidth(kDialogWidth);

    auto* blurb = new QLabel(
        tr("The shader starts from the template: the cbuffer layout, the ISF parameter "
           "block and a passthrough body, ready to edit."), this);
    blurb->setObjectName(QStringLiteral("Caption"));
    blurb->setWordWrap(true);
    Body()->addWidget(blurb);

    auto* label = new QLabel(tr("Name"), this);
    Body()->addWidget(label);

    m_name = new QLineEdit(this);
    m_name->setPlaceholderText(tr("e.g. chromatic drift"));
    m_name->setToolTip(tr("How the shader is listed in the library. It is not a filename; "
                          "use File > Save Shader As to write it to disk."));
    m_name->setClearButtonEnabled(true);
    connect(m_name, &QLineEdit::textChanged, this, &NewShaderDialog::SyncCreateButton);
    connect(m_name, &QLineEdit::returnPressed, this, [this] {
        if (m_create->isEnabled()) accept();
    });
    Body()->addWidget(m_name);

    QHBoxLayout* buttons = AddButtonRow();

    auto* cancel = new QPushButton(tr("Cancel"), this);
    cancel->setCursor(Qt::PointingHandCursor);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(cancel);

    m_create = new QPushButton(tr("Create"), this);
    m_create->setObjectName(QStringLiteral("Primary"));
    m_create->setCursor(Qt::PointingHandCursor);
    connect(m_create, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(m_create);

    SyncCreateButton();
    m_name->setFocus();
}

QString NewShaderDialog::Name() const
{
    return m_name->text().trimmed();
}

void NewShaderDialog::SyncCreateButton()
{
    // A shader with no name is a row in the library with nothing to click on, so the button
    // refuses visibly rather than creating one.
    const bool named = !Name().isEmpty();
    m_create->setEnabled(named);
    m_create->setToolTip(named ? tr("Add it to the library and open it in the editor.")
                               : tr("Give the shader a name first."));
}

QString NewShaderDialog::Create(Application& app, QWidget* parent)
{
    NewShaderDialog dialog(parent);
    if (dialog.exec() != QDialog::Accepted) return QString();

    const QString name = dialog.Name();
    if (name.isEmpty()) return QString();

    // Exactly the sequence the outgoing modal ran: parse the template's ISF block without
    // compiling, then AddPreset (the single point of vector sync, which compiles), then make
    // it active so the editor opens on it.
    ShaderManager& shaders = app.GetShaderManager();
    ShaderPreset preset;
    preset.name = name.toStdString();
    preset.source = ShaderManager::GetShaderTemplate();
    shaders.LoadShaderFromSource(preset.name, preset.source, preset);

    const int index = shaders.AddPreset(preset);
    shaders.SetActivePreset(index);
    return name;
}

}  // namespace SP
