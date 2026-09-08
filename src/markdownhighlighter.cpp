#include "markdownhighlighter.h"

#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QRawFont>
#include <QTextBlock>
#include <QTextDocument>

namespace {

const QRegularExpression &fenceRe() {
    static const QRegularExpression re(QStringLiteral("^\\s{0,3}(?:`{3,}|~{3,})"));
    return re;
}

const QRegularExpression &tableRowRe() {
    static const QRegularExpression re(QStringLiteral("^\\s*\\|.*\\|\\s*$"));
    return re;
}

const QRegularExpression &tableSeparatorRe() {
    static const QRegularExpression re(QStringLiteral("^[\\s|:-]+$"));
    return re;
}

const QRegularExpression &listRe() {
    static const QRegularExpression re(QStringLiteral(
        "^(\\s*(?:[-+*]|\\d+[.)])\\s+)(?:(\\[)([ xX])(\\]\\s?))?"));
    return re;
}

bool isFenceLine(const QString &line) {
    return fenceRe().match(line).hasMatch();
}

bool isTableRow(const QString &line) {
    return tableRowRe().match(line).hasMatch();
}

// `Title` over `===` is an H1 and over `---` an H2. Returns 0 for anything else.
int setextUnderlineLevel(const QString &line) {
    static const QRegularExpression re(QStringLiteral("^\\s{0,3}(=+|-+)\\s*$"));
    const QRegularExpressionMatch match = re.match(line);
    if (!match.hasMatch())
        return 0;
    return match.captured(1).startsWith(QLatin1Char('=')) ? 1 : 2;
}

// A setext underline only makes a heading of the line above if that line is
// ordinary prose. Anything that is already a block of its own stays what it is,
// which is what keeps `---` after a blank line a thematic break.
bool isParagraphLine(const QString &line) {
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty())
        return false;
    if (setextUnderlineLevel(line) > 0)
        return false;
    if (trimmed.startsWith(QLatin1Char('#')) || trimmed.startsWith(QLatin1Char('>')))
        return false;
    if (isFenceLine(line) || isTableRow(line))
        return false;
    return !listRe().match(line).hasMatch();
}

// QFontInfo::fixedPitch() reports false for iA Writer Mono S even though every
// advance is the same, so ask the metrics rather than the font's own claim.
bool isMonospaced(const QFont &font) {
    const QFontMetricsF metrics(font);
    return qFuzzyCompare(metrics.horizontalAdvance(QLatin1Char('i')),
                         metrics.horizontalAdvance(QLatin1Char('M')));
}

// QFontDatabase::systemFont() hands back the alias "monospace", which a
// QTextCharFormat cannot resolve: the run silently falls back to the document's
// own font. Resolve it to the family fontconfig actually picked.
QString resolvedFamily(const QFont &font) {
    const QString family = QRawFont::fromFont(font).familyName();
    return family.isEmpty() ? font.family() : family;
}

// Box-drawing characters join into continuous rules only when two things hold:
// they come from the same font as the code around them, and that font draws
// them at least as tall as the line they sit on. Having the glyphs is not
// enough. Noto Sans Mono, the system fixed font on this machine, draws U+2502
// at 0.92 of its own line spacing, which is a dashed line however the leading
// is set.
bool boxDrawingTiles(const QFont &font) {
    const QRawFont raw = QRawFont::fromFont(font);
    if (!raw.isValid())
        return false;
    for (const char32_t character : {U'\u2500', U'\u2502', U'\u250c', U'\u252c'}) {
        if (!raw.supportsCharacter(character))
            return false;
    }

    const QList<quint32> glyphs = raw.glyphIndexesForString(QStringLiteral("\u2502"));
    if (glyphs.isEmpty())
        return false;

    const qreal lineSpacing = QFontMetricsF(font).lineSpacing();
    return lineSpacing > 0 && raw.boundingRect(glyphs.constFirst()).height() >= lineSpacing;
}

// An unknown name is not a font: QFont substitutes silently, so asking the
// substitute whether it draws boxes answers about the wrong font.
bool isInstalled(const QString &family) {
    return QFontDatabase::families().contains(family, Qt::CaseInsensitive);
}

QFont probeFont(const QString &family) {
    QFont font(family);
    // Big enough that hinting cannot round the comparison the wrong way.
    font.setPixelSize(40);
    return font;
}

// The first installed monospace family that draws continuous boxes. Scanning
// every fixed-pitch family costs a few hundred milliseconds, so try the usual
// suspects first and remember the answer for the life of the process.
QString firstTilingMonospaceFamily() {
    static const QString cached = []() -> QString {
        const QStringList installed = QFontDatabase::families();
        const QStringList likely{QStringLiteral("DejaVu Sans Mono"),
                                 QStringLiteral("Liberation Mono"),
                                 QStringLiteral("Adwaita Mono"),
                                 QStringLiteral("Consolas")};
        for (const QString &family : likely) {
            if (installed.contains(family) && boxDrawingTiles(probeFont(family)))
                return family;
        }

        for (const QString &family : installed) {
            if (QFontDatabase::isFixedPitch(family) && boxDrawingTiles(probeFont(family)))
                return family;
        }
        return {};
    }();
    return cached;
}

} // namespace

bool MarkdownHighlighter::isTableRow(const QString &text) {
    return ::isTableRow(text);
}

bool MarkdownHighlighter::isFenceLine(const QString &text) {
    return ::isFenceLine(text);
}

bool MarkdownHighlighter::drawsContinuousBoxes(const QString &family) {
    return isInstalled(family) && boxDrawingTiles(probeFont(family));
}

void MarkdownHighlighter::setCodeFontFamily(const QString &family) {
    if (m_codeFontFamily == family)
        return;
    m_codeFontFamily = family;
    rebuildFormats();
    rehighlight();
}

QColor MarkdownHighlighter::codeBackgroundFor(const QString &pageBackground, bool darkMode) {
    QColor base(pageBackground);
    if (!base.isValid()) {
        base = darkMode ? QColor(QStringLiteral("#101010"))
                        : QColor(QStringLiteral("#ffffff"));
    }

    const QColor hsl = base.toHsl();
    const qreal lightness = hsl.lightnessF();
    // Darker than the page, which reads as an inset slab. On a page that is
    // already almost black there is nothing darker left to show, so step up
    // instead.
    constexpr qreal step = 0.04;
    const qreal wanted = lightness > step ? lightness - step : lightness + step;

    // hueF() is -1 for a grey; fromHslF rejects that.
    const qreal hue = hsl.hueF() < 0 ? 0.0 : hsl.hueF();
    const qreal saturation = hsl.hueF() < 0 ? 0.0 : hsl.saturationF();
    return QColor::fromHslF(hue, saturation, qBound(0.0, wanted, 1.0));
}

QList<int> MarkdownHighlighter::takeRestatedBlocks() {
    const QList<int> blocks = m_restatedBlocks;
    m_restatedBlocks.clear();
    return blocks;
}

// setCurrentBlockState, remembering which blocks it actually moved. A brand-new
// block reports -1, which is Normal as far as anything downstream cares.
void MarkdownHighlighter::applyBlockState(int state) {
    const int previous = currentBlock().userState();
    if ((previous < 0 ? int(Normal) : previous) != state)
        m_restatedBlocks.append(currentBlock().blockNumber());
    setCurrentBlockState(state);
}

MarkdownHighlighter::MarkdownHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document) {
    rebuildFormats();
}

void MarkdownHighlighter::setDarkMode(bool darkMode) {
    if (m_darkMode == darkMode)
        return;

    m_darkMode = darkMode;
    rebuildFormats();
    rehighlight();
}

void MarkdownHighlighter::setColors(const QString &background, const QString &foreground,
                                    const QString &accent) {
    if (m_customBackground == background && m_customForeground == foreground
            && m_customAccent == accent)
        return;

    m_customBackground = background;
    m_customForeground = foreground;
    m_customAccent = accent;
    rebuildFormats();
    rehighlight();
}

void MarkdownHighlighter::setSearch(const QString &query, int currentMatchStart) {
    if (m_searchQuery == query && m_currentMatchStart == currentMatchStart)
        return;
    m_searchQuery = query;
    m_currentMatchStart = currentMatchStart;
    rehighlight();
}

void MarkdownHighlighter::rebuildFormats() {
    const QColor marker = m_darkMode ? QColor(QStringLiteral("#4f525a"))
                                     : QColor(QStringLiteral("#aeb1b5"));
    const QColor background = !m_customBackground.isEmpty() ? QColor(m_customBackground)
        : (m_darkMode ? QColor(QStringLiteral("#101010")) : QColor(QStringLiteral("#ffffff")));
    const QColor text = !m_customForeground.isEmpty() ? QColor(m_customForeground)
        : (m_darkMode ? QColor(QStringLiteral("#eeeeee")) : QColor(QStringLiteral("#222324")));
    const QColor link = !m_customAccent.isEmpty() ? QColor(m_customAccent)
        : (m_darkMode ? QColor(QStringLiteral("#5584aa")) : QColor(QStringLiteral("#2077b2")));
    const QColor quote = marker;
    const QColor codeBackground = codeBackgroundFor(m_customBackground, m_darkMode);

    m_formatFont = document() ? document()->defaultFont() : QFont();

    // Tables and code only line up if every glyph on the line has the same
    // advance. Prefer the document's own font when it is monospaced, so a table
    // keeps the look of the rest of the page.
    const QFont systemFixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const QString systemFixed = resolvedFamily(systemFixedFont);

    m_tableFamilies.clear();
    if (isMonospaced(m_formatFont))
        m_tableFamilies.append(m_formatFont.family());
    m_tableFamilies.append(systemFixed);

    // Code has the stronger requirement: one font for the whole block, and that
    // font has to draw box characters that join, or every diagram in a fence
    // comes out in dashes.
    m_codeFamilies.clear();
    if (isInstalled(m_codeFontFamily))
        m_codeFamilies.append(m_codeFontFamily);
    if (m_codeFamilies.isEmpty() && isMonospaced(m_formatFont)
            && boxDrawingTiles(m_formatFont)) {
        m_codeFamilies.append(m_formatFont.family());
    }
    if (m_codeFamilies.isEmpty() && boxDrawingTiles(systemFixedFont))
        m_codeFamilies.append(systemFixed);
    if (m_codeFamilies.isEmpty()) {
        const QString tiling = firstTilingMonospaceFamily();
        if (!tiling.isEmpty())
            m_codeFamilies.append(tiling);
    }
    if (m_codeFamilies.isEmpty())
        m_codeFamilies = m_tableFamilies;

    m_markerFormat = QTextCharFormat();
    m_markerFormat.setForeground(marker);

    // A sub-pixel font size combined with a stretch factor used to make these
    // markers occupy (close to) zero space, but that combination deadlocks Qt's
    // font metrics engine on some platforms. Instead, use a normal font size and
    // cancel out its advance width with negative absolute letter-spacing.
    //
    // The metric is taken at 1pt, which is the size the marker is drawn at, so
    // it stays exact on a scaled heading line: the heading's own point size
    // never reaches the marker. In a fixed-pitch font one metric covers every
    // marker character, including the spaces inside a `## ` run.
    m_hiddenMarkerFormat = QTextCharFormat();
    m_hiddenMarkerFormat.setForeground(background);
    m_hiddenMarkerFormat.setFontPointSize(1.0);

    QFont hiddenFont = m_formatFont;
    hiddenFont.setPointSizeF(1.0);
    const qreal charWidth = QFontMetricsF(hiddenFont).horizontalAdvance(QLatin1Char('['));

    m_hiddenMarkerFormat.setFontLetterSpacingType(QFont::AbsoluteSpacing);
    m_hiddenMarkerFormat.setFontLetterSpacing(-charWidth);

    // H1 is half again as large as the body; H6 matches it and is told apart by
    // weight alone, the way the levels differ in print.
    static const qreal headingScale[6] = {1.6, 1.4, 1.25, 1.15, 1.05, 1.0};
    for (int level = 0; level < 6; ++level) {
        m_headingFormats[level] = QTextCharFormat();
        m_headingFormats[level].setForeground(text);
        m_headingFormats[level].setFontWeight(QFont::Bold);
        if (m_formatFont.pointSizeF() > 0) {
            m_headingFormats[level].setFontPointSize(m_formatFont.pointSizeF()
                                                     * headingScale[level]);
        } else if (m_formatFont.pixelSize() > 0) {
            // QTextCharFormat has no pixel-size setter, but the property is
            // read back by QTextCharFormat::font(). The editor sets its font in
            // pixels, so this is the branch that actually runs.
            m_headingFormats[level].setProperty(
                QTextFormat::FontPixelSize,
                qRound(m_formatFont.pixelSize() * headingScale[level]));
        }
    }

    m_boldFormat = QTextCharFormat();
    m_boldFormat.setFontWeight(QFont::Bold);
    m_boldFormat.setForeground(text);

    m_italicFormat = QTextCharFormat();
    m_italicFormat.setFontItalic(true);
    m_italicFormat.setForeground(text);

    m_strikeFormat = QTextCharFormat();
    m_strikeFormat.setFontStrikeOut(true);
    m_strikeFormat.setForeground(text);

    m_codeFormat = QTextCharFormat();
    m_codeFormat.setForeground(text);
    m_codeFormat.setBackground(codeBackground);
    m_codeFormat.setFontFamilies(m_codeFamilies);

    m_codeBlockFormat = QTextCharFormat();
    m_codeBlockFormat.setForeground(text);
    m_codeBlockFormat.setFontFamilies(m_codeFamilies);

    m_fenceFormat = QTextCharFormat();
    m_fenceFormat.setForeground(marker);
    m_fenceFormat.setFontFamilies(m_codeFamilies);

    m_tableFormat = QTextCharFormat();
    m_tableFormat.setForeground(text);
    m_tableFormat.setFontFamilies(m_tableFamilies);

    m_tablePipeFormat = m_tableFormat;
    m_tablePipeFormat.setForeground(marker);
    m_tableSeparatorFormat = m_tablePipeFormat;

    m_quoteFormat = QTextCharFormat();
    m_quoteFormat.setForeground(quote);
    m_quoteFormat.setFontItalic(true);

    m_linkFormat = QTextCharFormat();
    m_linkFormat.setForeground(link);
    m_linkFormat.setFontUnderline(true);

    m_searchFormat = QTextCharFormat();
    m_searchFormat.setBackground(m_darkMode ? QColor(QStringLiteral("#725b18"))
                                            : QColor(QStringLiteral("#ffe58a")));
    m_currentSearchFormat = QTextCharFormat();
    m_currentSearchFormat.setBackground(m_darkMode ? QColor(QStringLiteral("#b36b20"))
                                                   : QColor(QStringLiteral("#ffad42")));
}

void MarkdownHighlighter::highlightBlock(const QString &text) {
    // The desktop text size knob changes the document's default font without
    // touching its text, so nothing else would tell us the heading sizes and
    // the hidden-marker metric have gone stale.
    if (document() && m_formatFont != document()->defaultFont()) {
        rebuildFormats();
        QMetaObject::invokeMethod(this, "rehighlight", Qt::QueuedConnection);
    }

    if (highlightFencedCode(text)) {
        highlightSearch(text);
        return;
    }

    if (!text.isEmpty() && !highlightTableRow(text)) {
        highlightMarkers(text);
        highlightSetextContent(text);
        highlightInline(text);
    }

    highlightSearch(text);
}

// Everything between a pair of fences is code, so no inline rule may fire
// inside it. Without this a `*` in a shell glob comes out italic.
bool MarkdownHighlighter::highlightFencedCode(const QString &text) {
    const bool wasInside = previousBlockState() == InFencedCode;
    const bool onFenceLine = isFenceLine(text);

    if (!wasInside && !onFenceLine) {
        applyBlockState(Normal);
        return false;
    }

    applyBlockState(wasInside && onFenceLine ? Normal : InFencedCode);
    // The fence rows and the language tag stay dim; the code between them takes
    // the block background. An empty line inside a fence has no characters to
    // paint, so the background breaks there.
    setFormat(0, text.length(), onFenceLine ? m_fenceFormat : m_codeBlockFormat);
    return true;
}

bool MarkdownHighlighter::highlightTableRow(const QString &text) {
    if (!isTableRow(text))
        return false;

    if (tableSeparatorRe().match(text).hasMatch()) {
        setFormat(0, text.length(), m_tableSeparatorFormat);
        return true;
    }

    // Columns only line up while nothing on the line changes an advance width,
    // so inline styling does not run over a table row. inlineMarkup() skips
    // table rows for the same reason, which keeps the caret and the hidden
    // markers in agreement.
    setFormat(0, text.length(), m_tableFormat);
    for (int i = 0; i < text.length(); ++i) {
        if (text.at(i) == QLatin1Char('|'))
            setFormat(i, 1, m_tablePipeFormat);
    }
    return true;
}

void MarkdownHighlighter::highlightSearch(const QString &text) {
    if (m_searchQuery.isEmpty())
        return;

    int from = 0;
    while ((from = text.indexOf(m_searchQuery, from, Qt::CaseInsensitive)) >= 0) {
        const int documentStart = currentBlock().position() + from;
        QTextCharFormat format = this->format(from);
        format.setBackground(documentStart == m_currentMatchStart
                                 ? m_currentSearchFormat.background()
                                 : m_searchFormat.background());
        setFormat(from, m_searchQuery.length(), format);
        from += qMax(1, m_searchQuery.length());
    }
}

void MarkdownHighlighter::highlightMarkers(const QString &text) {
    int first = 0;
    while (first < text.length() && text.at(first).isSpace())
        ++first;
    if (first >= text.length())
        return;

    const QChar firstChar = text.at(first);

    if (setextUnderlineLevel(text) > 0
            && isParagraphLine(currentBlock().previous().text())) {
        setFormat(0, text.length(), m_markerFormat);
        scheduleSetextRefresh(currentBlock().previous().blockNumber());
        return;
    }

    if (firstChar == QLatin1Char('>')) {
        static const QRegularExpression quoteRe(QStringLiteral("^(\\s*>+\\s?)(.*)$"));
        const QRegularExpressionMatch quote = quoteRe.match(text);
        if (quote.hasMatch()) {
            setFormat(0, quote.capturedLength(1), m_markerFormat);
            setFormat(quote.capturedStart(2), quote.capturedLength(2), m_quoteFormat);
        }
    }

    if (firstChar == QLatin1Char('-') || firstChar == QLatin1Char('+')
            || firstChar == QLatin1Char('*') || firstChar.isDigit()) {
        const QRegularExpressionMatch list = listRe().match(text);
        if (list.hasMatch()) {
            setFormat(0, list.capturedLength(1), m_markerFormat);
            // `- [ ]` and `- [x]`: brackets dim like the bullet, the mark bold
            // so a finished item reads at a glance.
            if (list.capturedStart(2) >= 0) {
                const bool done = list.captured(3).compare(QStringLiteral("x"),
                                                           Qt::CaseInsensitive) == 0;
                setFormat(list.capturedStart(2), 1, m_markerFormat);
                setFormat(list.capturedStart(3), 1, done ? m_boldFormat : m_markerFormat);
                setFormat(list.capturedStart(4), list.capturedLength(4), m_markerFormat);
            }
        }
    }

    if (firstChar == QLatin1Char('-') || firstChar == QLatin1Char('*')
            || firstChar == QLatin1Char('_')) {
        static const QRegularExpression ruleRe(QStringLiteral("^\\s{0,3}([-*_])(?:\\s*\\1){2,}\\s*$"));
        const QRegularExpressionMatch rule = ruleRe.match(text);
        if (rule.hasMatch())
            setFormat(0, text.length(), m_markerFormat);
    }
}

// The heading is on this line, the underline that makes it one is on the next.
void MarkdownHighlighter::highlightSetextContent(const QString &text) {
    const int level = setextUnderlineLevel(currentBlock().next().text());
    if (level > 0 && isParagraphLine(text))
        setFormat(0, text.length(), m_headingFormats[level - 1]);
}

void MarkdownHighlighter::scheduleSetextRefresh(int blockNumber) {
    // The line above has already been highlighted, and it has no idea an
    // underline just appeared beneath it. Ask for it again once this pass ends.
    if (blockNumber < 0 || m_pendingSetextBlocks.contains(blockNumber))
        return;
    m_pendingSetextBlocks.append(blockNumber);
    if (m_pendingSetextBlocks.size() == 1)
        QMetaObject::invokeMethod(this, "refreshSetextHeadings", Qt::QueuedConnection);
}

void MarkdownHighlighter::refreshSetextHeadings() {
    const QList<int> blocks = m_pendingSetextBlocks;
    m_pendingSetextBlocks.clear();
    if (!document())
        return;
    for (int number : blocks) {
        const QTextBlock block = document()->findBlockByNumber(number);
        // A paragraph never schedules a refresh of its own, so this cannot
        // bounce between two lines.
        if (block.isValid())
            rehighlightBlock(block);
    }
}

void MarkdownHighlighter::highlightInline(const QString &text) {
    if (text.contains(QLatin1Char('`'))) {
        static const QRegularExpression codeRe(QStringLiteral("`([^`]+)`"));
        QRegularExpressionMatchIterator codeMatches = codeRe.globalMatch(text);
        while (codeMatches.hasNext()) {
            const QRegularExpressionMatch match = codeMatches.next();
            setFormat(match.capturedStart(0), match.capturedLength(0), m_codeFormat);
        }
    }

    const QList<InlineMarkup> markup = inlineMarkup(text);
    for (const InlineMarkup &item : markup) {
        if (item.kind == InlineKind::Heading) {
            setFormat(item.content.start, item.content.length,
                      m_headingFormats[qBound(1, item.level, 6) - 1]);
        } else {
            // Merge rather than replace, so `**bold**` inside a heading keeps
            // the heading's size and inline code keeps its background.
            QTextCharFormat merged = format(item.content.start);
            switch (item.kind) {
            case InlineKind::Bold:
                merged.setFontWeight(QFont::Bold);
                merged.setForeground(m_boldFormat.foreground());
                break;
            case InlineKind::Italic:
                merged.setFontItalic(true);
                merged.setForeground(m_italicFormat.foreground());
                break;
            case InlineKind::Strikethrough:
                merged.setFontStrikeOut(true);
                merged.setForeground(m_strikeFormat.foreground());
                break;
            case InlineKind::Link:
            case InlineKind::Image:
                merged.setFontUnderline(true);
                merged.setForeground(m_linkFormat.foreground());
                break;
            case InlineKind::Heading:
                break;
            }
            setFormat(item.content.start, item.content.length, merged);
        }

        for (const Span &marker : item.markers)
            setFormat(marker.start, marker.length, m_hiddenMarkerFormat);
    }
}

QList<MarkdownHighlighter::InlineMarkup>
MarkdownHighlighter::inlineMarkup(const QString &text, bool insideFencedCode) {
    QList<InlineMarkup> markup;
    // Code is not Markdown, and a table row is left alone so its columns stay
    // aligned. Neither hides a marker, so neither may report one.
    if (insideFencedCode || isFenceLine(text) || isTableRow(text))
        return markup;

    if (!text.contains(QLatin1Char('*')) && !text.contains(QLatin1Char('_'))
            && !text.contains(QLatin1Char('[')) && !text.contains(QLatin1Char('~'))
            && !text.startsWith(QLatin1Char('#'))) {
        return markup;
    }

    const auto span = [](const QRegularExpressionMatch &match, int group) {
        return Span{int(match.capturedStart(group)), int(match.capturedLength(group))};
    };

    // An ATX heading's `#` run and the space after it are hidden, not dimmed,
    // so the line reads as a heading rather than as its own syntax.
    static const QRegularExpression headingRe(QStringLiteral("^(#{1,6})(\\s+)(.*)$"));
    const QRegularExpressionMatch heading = headingRe.match(text);
    if (heading.hasMatch()) {
        const int markerLength = int(heading.capturedLength(1) + heading.capturedLength(2));
        markup.append({InlineKind::Heading, span(heading, 3),
                       {{0, markerLength}, {int(text.length()), 0}},
                       int(heading.capturedLength(1))});
    }

    static const QRegularExpression boldRe(QStringLiteral("(\\*\\*|__)(.+?)(\\1)"));
    QRegularExpressionMatchIterator boldMatches = boldRe.globalMatch(text);
    while (boldMatches.hasNext()) {
        const QRegularExpressionMatch match = boldMatches.next();
        markup.append({InlineKind::Bold, span(match, 2),
                       {span(match, 1), span(match, 3)}});
    }

    static const QRegularExpression italicRe(
        QStringLiteral("(?<!\\*)\\*([^*\\n]+)\\*(?!\\*)|(?<!_)_([^_\\n]+)_(?!_)"));
    QRegularExpressionMatchIterator italicMatches = italicRe.globalMatch(text);
    while (italicMatches.hasNext()) {
        const QRegularExpressionMatch match = italicMatches.next();
        const Span whole = span(match, 0);
        const int contentIndex = match.capturedStart(1) >= 0 ? 1 : 2;
        markup.append({InlineKind::Italic, span(match, contentIndex),
                       {{whole.start, 1}, {whole.start + whole.length - 1, 1}}});
    }

    static const QRegularExpression strikeRe(QStringLiteral("~~([^~\\n]+)~~"));
    QRegularExpressionMatchIterator strikeMatches = strikeRe.globalMatch(text);
    while (strikeMatches.hasNext()) {
        const QRegularExpressionMatch match = strikeMatches.next();
        const Span whole = span(match, 0);
        markup.append({InlineKind::Strikethrough, span(match, 1),
                       {{whole.start, 2}, {whole.start + whole.length - 2, 2}}});
    }

    // An image is a link with a `!` in front. Matching it first, and keeping
    // the link pattern from starting on a `[` that follows a `!`, stops the two
    // from claiming the same span.
    static const QRegularExpression imageRe(
        QStringLiteral("!\\[([^\\]]*)\\]\\(((?:\\\\.|[^)])*)\\)"));
    QRegularExpressionMatchIterator imageMatches = imageRe.globalMatch(text);
    while (imageMatches.hasNext()) {
        const QRegularExpressionMatch match = imageMatches.next();
        const Span whole = span(match, 0);
        const Span content = span(match, 1);
        const int contentEnd = content.start + content.length;
        markup.append({InlineKind::Image, content,
                       {{whole.start, 2},
                        {contentEnd, whole.start + whole.length - contentEnd}}});
    }

    static const QRegularExpression linkRe(
        QStringLiteral("(?<!!)\\[([^\\]]+)\\]\\(((?:\\\\.|[^)])+)\\)"));
    QRegularExpressionMatchIterator linkMatches = linkRe.globalMatch(text);
    while (linkMatches.hasNext()) {
        const QRegularExpressionMatch match = linkMatches.next();
        const Span whole = span(match, 0);
        const Span content = span(match, 1);
        const int contentEnd = content.start + content.length;
        markup.append({InlineKind::Link, content,
                       {{whole.start, 1},
                        {contentEnd, whole.start + whole.length - contentEnd}}});
    }

    return markup;
}
