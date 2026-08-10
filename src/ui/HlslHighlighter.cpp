#include "ui/HlslHighlighter.h"

#include "ui/Theme.h"

#include "ShaderCommonEmbedded.h"   // generated: kShaderCommonHLSL

#include <algorithm>

#include <QSet>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextLayout>

#include <definition.h>   // KSyntaxHighlighting; see the include note in HlslHighlighter.h
#include <repository.h>

namespace SP {

namespace {

// Tags a format run as comment, string or ISF block on its way into the block layout, so
// IsMarkupRegion can answer from outside highlightBlock (where QSyntaxHighlighter::format
// is unavailable) without keeping a parallel copy of the document's structure.
constexpr int kMarkupProperty = QTextFormat::UserProperty + 1;

bool IsMarkupStyle(KSyntaxHighlighting::Theme::TextStyle style)
{
    using TextStyle = KSyntaxHighlighting::Theme::TextStyle;
    switch (style) {
    case TextStyle::Comment:
    case TextStyle::Documentation:   // the ISF block
    case TextStyle::Annotation:
    case TextStyle::CommentVar:
    case TextStyle::RegionMarker:
    case TextStyle::Alert:
    case TextStyle::Char:
    case TextStyle::SpecialChar:
    case TextStyle::String:
    case TextStyle::VerbatimString:
    case TextStyle::SpecialString:
        return true;
    default:
        return false;
    }
}

// One repository for the process. It is an immutable index of definitions and themes built
// by scanning resources, costs a double-digit millisecond scan to construct, and is meant
// to be shared; its size is fixed by what is compiled into the binary rather than growing
// with anything the user does, so it is not the unbounded process-lifetime cache the
// project rule is about. Constructed on the first highlighter, which is after QApplication
// and therefore after the qrc is mounted.
struct RepositoryHolder {
    KSyntaxHighlighting::Repository repository;

    RepositoryHolder()
    {
        // Scans <path>/syntax and <path>/themes, never <path> itself — see the aliasing
        // note in resources/resources.qrc.
        repository.addCustomSearchPath(QStringLiteral(":/syntax"));
    }
};

KSyntaxHighlighting::Repository& SharedRepository()
{
    // Repository is neither copyable nor movable, so it is constructed in place.
    static RepositoryHolder holder;
    return holder.repository;
}

} // namespace

const QStringList& HlslHighlighter::PreambleNames()
{
    // Parsed from the copy of ShaderCommon.hlsli that CMake embeds in the executable, so
    // the list cannot drift from what BuildDefinesPreamble actually injects and there is
    // no runtime file to find.
    //
    // Both patterns are deliberately narrow. A bare \bsp\w* would also match "space",
    // "spans" and "specific" out of the file's own prose comments; requiring the
    // sp<Uppercase> spelling of the naming convention plus a following call paren, and
    // SP_ only where it is #defined, matches the 20 helpers and 2 macros and nothing else.
    static const QStringList names = [] {
        static const QRegularExpression functionRe(
            QStringLiteral("\\b(sp[A-Z][A-Za-z0-9_]*)\\s*\\("));
        static const QRegularExpression macroRe(
            QStringLiteral("#\\s*define\\s+(SP_[A-Za-z0-9_]+)"));

        const QString source = QString::fromUtf8(kShaderCommonHLSL);
        QSet<QString> unique;
        for (const QRegularExpression* re : {&functionRe, &macroRe}) {
            QRegularExpressionMatchIterator it = re->globalMatch(source);
            while (it.hasNext()) {
                unique.insert(it.next().captured(1));
            }
        }

        QStringList out(unique.cbegin(), unique.cend());
        out.sort();
        return out;
    }();
    return names;
}

HlslHighlighter::HlslHighlighter(QTextDocument* document)
    : KSyntaxHighlighting::SyntaxHighlighter(document)
{
    KSyntaxHighlighting::Repository& repository = SharedRepository();

    m_theme = repository.theme(QStringLiteral("ShaderPlayer Dark"));
    if (!m_theme.isValid()) {
        // The bundled theme failed to load. A stock dark theme keeps the editor readable
        // rather than leaving every run of text on the default palette.
        m_theme = repository.defaultTheme(KSyntaxHighlighting::Repository::DarkTheme);
    }
    setTheme(m_theme);

    // An invalid definition is not fatal: the editor still edits, it just stops colouring.
    setDefinition(repository.definitionForName(QStringLiteral("HLSL")));

    RebuildInjectedPattern();
}

void HlslHighlighter::SetParamNames(const QStringList& names)
{
    if (names == m_paramNames) return;
    m_paramNames = names;
    RebuildInjectedPattern();
    rehighlight();
}

void HlslHighlighter::RebuildInjectedPattern()
{
    QStringList alternatives;
    alternatives.reserve(PreambleNames().size() + m_paramNames.size());
    for (const QString& name : PreambleNames()) {
        alternatives << QRegularExpression::escape(name);
    }
    // Parameter names come out of a shader file, which is untrusted text: escaped, never
    // pasted into the pattern raw.
    for (const QString& name : m_paramNames) {
        if (name.isEmpty()) continue;
        alternatives << QRegularExpression::escape(name);
    }

    if (alternatives.isEmpty()) {
        m_injected = QRegularExpression();
        return;
    }
    m_injected = QRegularExpression(QStringLiteral("\\b(?:%1)\\b")
                                        .arg(alternatives.join(QLatin1Char('|'))));
    m_injected.optimize();
}

void HlslHighlighter::applyFormat(int offset, int length,
                                  const KSyntaxHighlighting::Format& format)
{
    SyntaxHighlighter::applyFormat(offset, length, format);

    if (length <= 0 || !IsMarkupStyle(format.textStyle())) return;

    m_markupSpans.append(Span{offset, length});

    QTextCharFormat tagged = SyntaxHighlighter::format(offset);
    tagged.setProperty(kMarkupProperty, true);
    setFormat(offset, length, tagged);
}

void HlslHighlighter::highlightBlock(const QString& text)
{
    m_markupSpans.clear();
    SyntaxHighlighter::highlightBlock(text);

    if (m_injected.pattern().isEmpty()) return;

    // Second pass. It repaints only the foreground of what the grammar already produced,
    // so an injected name keeps whatever weight, slant and background the theme gave the
    // run it sits in, and picks up accent-quaternary on top. Markup runs are skipped: the
    // ISF block declares these names and already reads as its own region, and re-tinting
    // them there would fight it.
    const QColor injectedColour = Theme::kAccentQuaternary;
    QRegularExpressionMatchIterator it = m_injected.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const int start = match.capturedStart();
        const int length = match.capturedLength();

        const bool inMarkup = std::any_of(
            m_markupSpans.cbegin(), m_markupSpans.cend(), [start](const Span& span) {
                return start >= span.offset && start < span.offset + span.length;
            });
        if (inMarkup) continue;

        QTextCharFormat injected = format(start);
        injected.setForeground(injectedColour);
        setFormat(start, length, injected);
    }
}

bool HlslHighlighter::IsMarkupRegion(const QTextBlock& block, int posInBlock) const
{
    const QTextLayout* layout = block.layout();
    if (!layout) return false;

    const QList<QTextLayout::FormatRange> ranges = layout->formats();
    for (const QTextLayout::FormatRange& range : ranges) {
        if (posInBlock >= range.start && posInBlock < range.start + range.length) {
            return range.format.property(kMarkupProperty).toBool();
        }
    }
    return false;
}

QStringList HlslHighlighter::CompletionWords() const
{
    static const QStringList kLists = {
        QStringLiteral("intrinsics"),  QStringLiteral("types"),
        QStringLiteral("keywords"),    QStringLiteral("controlflow"),
        QStringLiteral("typequal"),    QStringLiteral("semantics"),
    };

    QSet<QString> unique;
    const KSyntaxHighlighting::Definition hlsl = definition();
    for (const QString& list : kLists) {
        const QStringList words = hlsl.keywordList(list);
        for (const QString& word : words) unique.insert(word);
    }
    for (const QString& name : PreambleNames()) unique.insert(name);
    for (const QString& name : m_paramNames) {
        if (!name.isEmpty()) unique.insert(name);
    }

    QStringList out(unique.cbegin(), unique.cend());
    out.sort(Qt::CaseInsensitive);
    return out;
}

} // namespace SP
