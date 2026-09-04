#include "ui/EditorPanel.h"

#include "Application.h"
#include "ShaderManager.h"
#include "ui/HlslHighlighter.h"
#include "ui/Theme.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStringListModel>
#include <QStyle>
#include <QTextBlock>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

namespace SP {

namespace {

using EditorColor = KSyntaxHighlighting::Theme::EditorColorRole;

QColor ThemeColour(const KSyntaxHighlighting::Theme& theme, EditorColor role)
{
    return QColor::fromRgba(theme.editorColor(role));
}

// Gutter geometry. The mark strip on the left is where a failed compile puts its bar, and
// it is reserved at all times so the numbers do not shift sideways when an error appears.
constexpr int kGutterMarkWidth = 4;
constexpr int kGutterPadLeft   = 6;
constexpr int kGutterPadRight  = 8;
constexpr int kGutterMinDigits = 3;

// Brace matching walks the document by hand. A cap keeps an unbalanced brace in a large
// file from turning every keystroke into a full-document scan; beyond it the match is
// simply not shown, which is the same outcome as no partner existing.
constexpr int kBraceScanLimit = 40000;

// The completer stays out of the way until there is enough of a word to be useful.
constexpr int kCompletionPrefixMin = 2;

// fxc reports "<name>(line,column): error X0000: ...". The preamble ends in `#line 1`, so
// the number is already the line in the file the author is looking at.
int ParseErrorLine(const QString& message)
{
    static const QRegularExpression re(QStringLiteral("\\((\\d+),\\s*\\d+\\)\\s*:"));
    const QRegularExpressionMatch match = re.match(message);
    if (!match.hasMatch()) return 0;

    bool ok = false;
    const int line = match.captured(1).toInt(&ok);
    return (ok && line > 0) ? line : 0;
}

// The gutter is a real widget rather than a painted margin so it scrolls with the viewport
// and can be sized independently of the text. It carries no state: SourceEdit paints it.
class EditorGutter : public QWidget {
public:
    explicit EditorGutter(SourceEdit* edit)
        : QWidget(edit)
        , m_edit(edit)
    {
    }

    QSize sizeHint() const override { return QSize(m_edit->GutterWidth(), 0); }

protected:
    void paintEvent(QPaintEvent* event) override { m_edit->PaintGutter(event); }

private:
    SourceEdit* m_edit;
};

} // namespace

// ---------------------------------------------------------------------------------------
// SourceEdit
// ---------------------------------------------------------------------------------------

SourceEdit::SourceEdit(QWidget* parent)
    : QPlainTextEdit(parent)
{
    // Picks up the Consolas 10pt and the recessed input styling from the stylesheet; the
    // font is also set outright because the tab stop is computed from its metrics before
    // the widget is ever polished.
    setObjectName(QStringLiteral("ShaderEditor"));

    QFont mono(QString::fromLatin1(Theme::kFontMono), Theme::kFontSizeBody);
    mono.setStyleHint(QFont::Monospace);
    setFont(mono);
    setTabStopDistance(QFontMetricsF(mono).horizontalAdvance(QLatin1Char(' ')) * 4.0);

    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFrameShape(QFrame::NoFrame);   // the stylesheet's border is the frame
    setPlaceholderText(tr("No shader open.\n\n"
                          "Pick one from the Shader Library, or drop a .hlsl file "
                          "anywhere on the window."));

    m_gutter = new EditorGutter(this);

    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] {
        setViewportMargins(GutterWidth(), 0, 0, 0);
        m_gutter->update();
    });
    connect(this, &QPlainTextEdit::updateRequest, this,
            [this](const QRect& rect, int dy) {
                if (dy != 0) {
                    m_gutter->scroll(0, dy);
                } else {
                    m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
                }
            });

    setViewportMargins(GutterWidth(), 0, 0, 0);
}

void SourceEdit::SetTheme(const KSyntaxHighlighting::Theme& theme)
{
    m_theme = theme;
    m_gutter->update();
}

void SourceEdit::SetCompleter(QCompleter* completer)
{
    m_completer = completer;
    if (!m_completer) return;

    m_completer->setWidget(this);
    connect(m_completer, QOverload<const QString&>::of(&QCompleter::activated), this,
            &SourceEdit::InsertCompletion);
}

void SourceEdit::SetErrorLine(int line)
{
    if (line == m_errorLine) return;
    m_errorLine = line;
    m_gutter->update();
}

int SourceEdit::GutterWidth() const
{
    int digits = 1;
    for (int count = qMax(1, blockCount()); count >= 10; count /= 10) ++digits;
    digits = qMax(digits, kGutterMinDigits);

    return kGutterMarkWidth + kGutterPadLeft
         + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits + kGutterPadRight;
}

void SourceEdit::PaintGutter(QPaintEvent* event)
{
    QPainter painter(m_gutter);
    painter.fillRect(event->rect(), ThemeColour(m_theme, EditorColor::IconBorder));

    // The editor's own region hairline, in the same violet the dock carries.
    const QColor separator = ThemeColour(m_theme, EditorColor::Separator);
    painter.fillRect(m_gutter->width() - 1, event->rect().y(), 1, event->rect().height(),
                     separator);

    const QColor numbers  = ThemeColour(m_theme, EditorColor::LineNumbers);
    const QColor current  = ThemeColour(m_theme, EditorColor::CurrentLineNumber);
    const QColor errorCol = ThemeColour(m_theme, EditorColor::MarkError);
    const int    cursorLine = textCursor().blockNumber();

    QTextBlock block = firstVisibleBlock();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());

    QFont numberFont = font();
    while (block.isValid() && top <= event->rect().bottom()) {
        const int bottom = top + qRound(blockBoundingRect(block).height());
        if (block.isVisible() && bottom >= event->rect().top()) {
            const bool isCursor = block.blockNumber() == cursorLine;
            const bool isError  = m_errorLine > 0 && block.blockNumber() == m_errorLine - 1;

            if (isError) {
                painter.fillRect(0, top, kGutterMarkWidth, bottom - top, errorCol);
            }

            numberFont.setBold(isCursor);
            painter.setFont(numberFont);
            painter.setPen(isError ? errorCol : (isCursor ? current : numbers));
            painter.drawText(0, top,
                             m_gutter->width() - kGutterPadRight, bottom - top,
                             Qt::AlignRight | Qt::AlignVCenter,
                             QString::number(block.blockNumber() + 1));
        }
        block = block.next();
        top = bottom;
    }
}

void SourceEdit::resizeEvent(QResizeEvent* event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect content = contentsRect();
    m_gutter->setGeometry(content.left(), content.top(), GutterWidth(), content.height());
}

QString SourceEdit::WordUnderCursor() const
{
    QTextCursor cursor = textCursor();
    cursor.select(QTextCursor::WordUnderCursor);
    return cursor.selectedText();
}

void SourceEdit::InsertCompletion(const QString& completion)
{
    if (!m_completer || m_completer->widget() != this) return;

    QTextCursor cursor = textCursor();
    const int tail = completion.length() - m_completer->completionPrefix().length();
    if (tail < 0) return;
    cursor.movePosition(QTextCursor::Left);
    cursor.movePosition(QTextCursor::EndOfWord);
    cursor.insertText(completion.right(tail));
    setTextCursor(cursor);
}

void SourceEdit::keyPressEvent(QKeyEvent* event)
{
    const bool popupVisible = m_completer && m_completer->popup()->isVisible();
    if (popupVisible) {
        // The popup owns these; letting the editor see them would insert a newline behind
        // the completion or move the caret out from under the list.
        switch (event->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Escape:
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            event->ignore();
            return;
        default:
            break;
        }
    }

    QPlainTextEdit::keyPressEvent(event);
    if (!m_completer) return;

    const QString prefix = WordUnderCursor();
    if (prefix.length() < kCompletionPrefixMin
        || (!event->text().isEmpty() && !event->text().at(0).isLetterOrNumber()
            && event->text() != QLatin1String("_"))) {
        m_completer->popup()->hide();
        return;
    }

    if (prefix != m_completer->completionPrefix()) {
        m_completer->setCompletionPrefix(prefix);
        m_completer->popup()->setCurrentIndex(m_completer->completionModel()->index(0, 0));
    }
    if (m_completer->completionCount() == 0) {
        m_completer->popup()->hide();
        return;
    }

    QRect box = cursorRect();
    box.setWidth(m_completer->popup()->sizeHintForColumn(0)
                 + m_completer->popup()->verticalScrollBar()->sizeHint().width());
    m_completer->complete(box);
}

// ---------------------------------------------------------------------------------------
// EditorPanel
// ---------------------------------------------------------------------------------------

EditorPanel::EditorPanel(Application& app, QWidget* parent)
    : QWidget(parent)
    , m_app(app)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Theme::kSpaceUnit);

    // ---- header ------------------------------------------------------------------------
    auto* header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(Theme::kSpaceUnit);

    m_compileButton = new QPushButton(tr("Compile (F5)"), this);
    m_compileButton->setObjectName(QStringLiteral("Primary"));
    m_compileButton->setToolTip(tr("Compile the editor contents into the active shader."));
    connect(m_compileButton, &QPushButton::clicked, this, &EditorPanel::Compile);
    header->addWidget(m_compileButton);

    header->addStretch(1);

    m_status = new QLabel(this);
    m_status->hide();   // no active preset yet, so there is no compile state to report
    header->addWidget(m_status);

    layout->addLayout(header);

    // ---- document ----------------------------------------------------------------------
    m_edit = new SourceEdit(this);
    layout->addWidget(m_edit, 1);

    m_highlighter = new HlslHighlighter(m_edit->document());
    m_edit->SetTheme(m_highlighter->EditorTheme());

    m_completionModel = new QStringListModel(this);
    m_completer = new QCompleter(m_completionModel, this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setWrapAround(false);
    m_edit->SetCompleter(m_completer);
    RefreshCompletionModel();

    // ---- footer ------------------------------------------------------------------------
    m_footer = new QLabel(this);
    m_footer->setObjectName(QStringLiteral("StatusError"));
    m_footer->setWordWrap(true);
    m_footer->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_footer->hide();
    layout->addWidget(m_footer);

    // ---- auto-compile --------------------------------------------------------------------
    // Same behaviour the immediate-mode editor had: a quiet period after the last
    // keystroke, its length and its on/off switch both read live from the config.
    m_autoCompile = new QTimer(this);
    m_autoCompile->setSingleShot(true);
    connect(m_autoCompile, &QTimer::timeout, this, &EditorPanel::Compile);

    connect(m_edit, &QPlainTextEdit::textChanged, this, &EditorPanel::RestartAutoCompile);
    connect(m_edit, &QPlainTextEdit::cursorPositionChanged, this,
            &EditorPanel::UpdateExtraSelections);

    UpdateExtraSelections();
}

QString EditorPanel::Source() const
{
    return m_edit->toPlainText();
}

void EditorPanel::SetSource(const QString& source)
{
    m_autoCompile->stop();
    {
        // Loading a preset is not an edit, so it must not start the auto-compile clock.
        const QSignalBlocker blocker(m_edit);
        m_edit->setPlainText(source);
    }

    // Neutral until something compiles. This is the no-preset state the immediate-mode
    // editor showed by drawing no status at all.
    m_status->hide();
    m_footer->hide();
    m_edit->SetErrorLine(0);

    UpdateExtraSelections();
}

bool EditorPanel::IsFocused() const
{
    return m_edit->hasFocus();
}

void EditorPanel::ShowCompileResult(bool ok, const QString& error)
{
    m_status->setText(ok ? tr("OK") : tr("Error"));
    m_status->setObjectName(ok ? QStringLiteral("StatusOk") : QStringLiteral("StatusError"));
    // An object name changed after construction does not re-run the stylesheet by itself.
    m_status->style()->unpolish(m_status);
    m_status->style()->polish(m_status);
    m_status->show();

    if (ok) {
        m_footer->hide();
        m_edit->SetErrorLine(0);
        return;
    }

    const QString message = error.isEmpty() ? tr("Compilation failed.") : error;
    m_footer->setText(message);
    m_footer->show();

    const int line = ParseErrorLine(message);
    m_edit->SetErrorLine(line <= m_edit->blockCount() ? line : 0);
}

void EditorPanel::SetParamNames(const QStringList& names)
{
    m_highlighter->SetParamNames(names);
    RefreshCompletionModel();
}

void EditorPanel::RefreshCompletionModel()
{
    m_completionModel->setStringList(m_highlighter->CompletionWords());
}

void EditorPanel::RestartAutoCompile()
{
    const AppConfig& config = m_app.GetConfig();
    if (!config.autoCompileOnSave) {
        m_autoCompile->stop();
        return;
    }
    m_autoCompile->start(qMax(0, config.autoCompileDelayMs));
}

void EditorPanel::Compile()
{
    m_autoCompile->stop();

    // Quiet: this fires from the auto-compile timer half a second after the last keystroke
    // as well as from F5, and a toast on every typing pause (success and failure both) is
    // noise over a status line that already says the same thing.
    const bool ok = m_app.CompileCurrentShader(Source().toStdString(), true);

    QString error;
    if (const ShaderPreset* preset = m_app.GetShaderManager().GetActivePreset()) {
        error = QString::fromStdString(preset->compileError);
    }
    ShowCompileResult(ok, error);
}

void EditorPanel::UpdateExtraSelections()
{
    const KSyntaxHighlighting::Theme& theme = m_highlighter->EditorTheme();
    QList<QTextEdit::ExtraSelection> selections;

    QTextEdit::ExtraSelection currentLine;
    currentLine.format.setBackground(ThemeColour(theme, EditorColor::CurrentLine));
    currentLine.format.setProperty(QTextFormat::FullWidthSelection, true);
    currentLine.cursor = m_edit->textCursor();
    currentLine.cursor.clearSelection();
    selections.append(currentLine);

    // ---- brace matching ------------------------------------------------------------------
    // Only ever anchored on a brace the grammar did not mark as comment, string or ISF
    // block, and it steps over every such run while searching, so a stray brace inside a
    // comment neither starts a match nor breaks one.
    const QTextDocument* document = m_edit->document();
    const int cursorPos = m_edit->textCursor().position();

    static const QString kOpeners = QStringLiteral("([{");
    static const QString kClosers = QStringLiteral(")]}");

    auto isMarkup = [this, document](int position) {
        const QTextBlock block = document->findBlock(position);
        return !block.isValid()
            || m_highlighter->IsMarkupRegion(block, position - block.position());
    };

    auto findPartner = [&](int from, QChar self, QChar partner, int step) {
        int depth = 0;
        for (int i = 0, at = from; i < kBraceScanLimit; ++i, at += step) {
            if (at < 0 || at >= document->characterCount()) return -1;
            const QChar ch = document->characterAt(at);
            if (ch != self && ch != partner) continue;
            if (isMarkup(at)) continue;
            depth += (ch == self) ? 1 : -1;
            if (depth == 0) return at;
        }
        return -1;
    };

    // Either the character the caret sits on (an opener) or the one behind it (a closer),
    // which is where the caret lands after typing the brace.
    int bracePos = -1;
    int partnerPos = -1;
    for (const int candidate : {cursorPos, cursorPos - 1}) {
        if (candidate < 0 || candidate >= document->characterCount()) continue;
        const QChar ch = document->characterAt(candidate);
        const int openIndex  = kOpeners.indexOf(ch);
        const int closeIndex = kClosers.indexOf(ch);
        if (openIndex < 0 && closeIndex < 0) continue;
        if (isMarkup(candidate)) continue;

        const QChar partner = openIndex >= 0 ? kClosers.at(openIndex) : kOpeners.at(closeIndex);
        const int step = openIndex >= 0 ? 1 : -1;
        partnerPos = findPartner(candidate, ch, partner, step);
        if (partnerPos >= 0) {
            bracePos = candidate;
            break;
        }
    }

    if (bracePos >= 0) {
        const QColor match = ThemeColour(theme, EditorColor::BracketMatching);
        for (const int position : {bracePos, partnerPos}) {
            QTextEdit::ExtraSelection selection;
            selection.format.setBackground(match);
            selection.cursor = QTextCursor(m_edit->document());
            selection.cursor.setPosition(position);
            selection.cursor.movePosition(QTextCursor::NextCharacter,
                                          QTextCursor::KeepAnchor);
            selections.append(selection);
        }
    }

    m_edit->setExtraSelections(selections);
}

} // namespace SP
