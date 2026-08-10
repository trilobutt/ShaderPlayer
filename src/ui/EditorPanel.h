#pragma once

// EditorPanel.h — the body of the Shader Editor dock.
//
// KSyntaxHighlighting colours the text (see HlslHighlighter). Everything a code editor needs
// beyond colour is here: the line-number gutter, the current-line band, brace matching, an
// intrinsic/parameter completer, and the compile-error mark that ties an fxc message to the
// line that produced it. fxc line numbers already refer to the file on disk, because the
// injected preamble ends in `#line 1`, so no offset correction is applied anywhere below.
//
// This panel, not the active preset, owns the document. Everything that consumes "the
// editor source" (File > Save Shader, Save Shader As, Shader > Compile) reads Source().

#include <QPlainTextEdit>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <theme.h>   // KSyntaxHighlighting; see the include note in HlslHighlighter.h

QT_BEGIN_NAMESPACE
class QCompleter;
class QLabel;
class QPaintEvent;
class QPushButton;
class QResizeEvent;
class QStringListModel;
class QTimer;
QT_END_NAMESPACE

namespace SP {

class Application;
class HlslHighlighter;

// The text surface. QPlainTextEdit keeps block geometry protected, so the gutter cannot be
// drawn from outside; this subclass exists to expose it and to intercept the keys the
// completer popup needs.
class SourceEdit : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit SourceEdit(QWidget* parent = nullptr);

    void SetTheme(const KSyntaxHighlighting::Theme& theme);
    void SetCompleter(QCompleter* completer);

    // 1-based, 0 for none. Draws the state-error mark beside that line in the gutter.
    void SetErrorLine(int line);
    int ErrorLine() const { return m_errorLine; }

    int GutterWidth() const;
    void PaintGutter(QPaintEvent* event);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void InsertCompletion(const QString& completion);
    QString WordUnderCursor() const;

    QWidget* m_gutter = nullptr;
    QCompleter* m_completer = nullptr;
    KSyntaxHighlighting::Theme m_theme;
    int m_errorLine = 0;
};

class EditorPanel : public QWidget {
    Q_OBJECT
public:
    explicit EditorPanel(Application& app, QWidget* parent = nullptr);

    QString Source() const;
    void SetSource(const QString& source);
    bool IsFocused() const;
    void ShowCompileResult(bool ok, const QString& error);

    // The active preset's ISF parameter names: highlighted as injected identifiers and
    // offered by the completer. Called from MainWindow::RefreshParameters.
    void SetParamNames(const QStringList& names);

private:
    void Compile();
    void RestartAutoCompile();
    void UpdateExtraSelections();
    void RefreshCompletionModel();

    Application& m_app;

    SourceEdit* m_edit = nullptr;
    HlslHighlighter* m_highlighter = nullptr;
    QPushButton* m_compileButton = nullptr;
    QLabel* m_status = nullptr;    // "OK" / "Error" beside the compile button
    QLabel* m_footer = nullptr;    // the compile message; hidden while there is none
    QTimer* m_autoCompile = nullptr;
    QCompleter* m_completer = nullptr;
    QStringListModel* m_completionModel = nullptr;
};

} // namespace SP
