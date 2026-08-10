#pragma once

// HlslHighlighter.h — HLSL syntax highlighting for the shader editor document.
//
// The grammar and the colours are data: resources/syntax/hlsl.xml describes the language
// and resources/syntax/shaderplayer-dark.theme describes how it looks, both loaded through
// KSyntaxHighlighting. Nothing about HLSL is spelled out in this class.
//
// What is spelled out here is the part no stock grammar can know, because it does not come
// from the file on disk at all. ShaderManager::BuildDefinesPreamble injects two families of
// identifier ahead of every compile: the sp*/SP_ helpers from src/ShaderCommon.hlsli, and a
// #define alias per ISF parameter of the active preset. Both are undeclared as far as the
// editor's text is concerned. A second highlight pass paints them in accent-quaternary, so
// an author reading a shader can tell at a glance which names their file defines and which
// ones arrive from the preamble.

#include <QList>
#include <QRegularExpression>
#include <QStringList>

// The lowercase headers, not KSyntaxHighlighting's CamelCase forwarders. A forwarder is a
// one-line `#include "theme.h"`, and MSVC resolves a quoted include against the directories
// of every file already open on the include stack before it reaches the -I list. Any header
// in src/ui therefore resolves that line to this project's own ui/Theme.h on a
// case-insensitive filesystem, and the KSyntaxHighlighting namespace never appears. The
// real headers quote each other from their own directory, where the match is correct.
#include <format.h>
#include <syntaxhighlighter.h>
#include <theme.h>

QT_BEGIN_NAMESPACE
class QTextBlock;
class QTextDocument;
QT_END_NAMESPACE

namespace SP {

class HlslHighlighter : public KSyntaxHighlighting::SyntaxHighlighter {
    Q_OBJECT
public:
    explicit HlslHighlighter(QTextDocument* document);

    // The active preset's ISF parameter names, from MainWindow::RefreshParameters.
    // Rehighlights only when the set actually differs.
    void SetParamNames(const QStringList& names);

    // Everything the completer should offer: the keyword, type and intrinsic lists read
    // back out of the loaded definition, plus the preamble names and current parameters.
    // Reading them from the definition keeps hlsl.xml the single source of truth for what
    // HLSL contains, rather than mirroring the same word list in C++.
    QStringList CompletionWords() const;

    // True when `posInBlock` falls inside a comment, a string or the ISF block. Brace
    // matching consults this so a brace inside a comment never pairs with real code.
    bool IsMarkupRegion(const QTextBlock& block, int posInBlock) const;

    // The theme actually in force, so the editor chrome (gutter, current-line band, brace
    // match, error mark) can take its colours from the same file the syntax does.
    const KSyntaxHighlighting::Theme& EditorTheme() const { return m_theme; }

    // The sp*/SP_ helpers, parsed once from the embedded copy of ShaderCommon.hlsli.
    static const QStringList& PreambleNames();

protected:
    void highlightBlock(const QString& text) override;
    void applyFormat(int offset, int length, const KSyntaxHighlighting::Format& format) override;

private:
    void RebuildInjectedPattern();

    struct Span {
        int offset;
        int length;
    };

    KSyntaxHighlighting::Theme m_theme;
    QStringList m_paramNames;
    QRegularExpression m_injected;   // empty pattern = second pass disabled
    QList<Span> m_markupSpans;       // comment/string/ISF runs of the block being highlighted
};

} // namespace SP
