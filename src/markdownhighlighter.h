#pragma once

#include <QList>
#include "codesyntaxhighlighter.h"

#include <QColor>
#include <QHash>
#include <QSet>
#include <memory>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

class MarkdownHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit MarkdownHighlighter(QTextDocument *document);

    void setDarkMode(bool darkMode);
    void setColors(const QString &background, const QString &foreground, const QString &accent);
    void setSearch(const QString &query, int currentMatchStart);

    // The block the caret is in shows its markers as written. Everywhere
    // else they are hidden and something is drawn in their place.
    void setActiveBlock(int blockNumber);

    // Blocks of the fenced run the caret is in, whose fence rows show their
    // backticks. Everywhere else the rows collapse into the slab.
    void setRevealedRange(int firstBlock, int lastBlock);

    // Rows of tables whose columns are drawn as rules. Their pipes fold
    // away; a table without a grid keeps the pipes it was written with.
    void setGriddedRows(const QSet<int> &blockNumbers);

    // Carried on the block's user state so the next block knows whether it is
    // inside a fence, and so Backend::hiddenRangesAt can tell that a line of
    // code is not a line of Markdown.
    //
    // A fenced line also carries which language it is in and where the lexer
    // had got to at the end of the line, packed into the same int because that
    // is all QSyntaxHighlighter gives us:
    //
    //   bits 0-7   flag, Normal or InFencedCode
    //   bits 8-15  index into the languages seen in this document
    //   bits 16-23 the code highlighter's own state, for block comments
    enum BlockState { Normal = 0, InFencedCode = 1, ThematicBreak = 2, TableRow = 3 };

    static bool isFencedState(int state) {
        return state > 0 && (state & 0xff) == InFencedCode;
    }

    struct Span {
        int start;
        int length;
    };

    enum class InlineKind { Bold, Italic, BoldItalic, Strikethrough, Link, Image,
                            Heading, Code };

    struct InlineMarkup {
        InlineKind kind;
        Span content;
        Span markers[2];
        int level = 0; // heading level, 1 to 6; unused by every other kind
    };

    // Single source of truth for markdown spans whose markers are hidden: the
    // highlighter uses it to style content and hide markers, and the editor
    // uses it (via Backend::hiddenRangesAt) to skip the caret over them. A
    // hidden marker missing from this list traps the cursor; a marker listed
    // here but left visible makes the caret jump over nothing.
    static QList<InlineMarkup> inlineMarkup(const QString &text,
                                            bool insideFencedCode = false);

    // A table row is styled as a unit, so anything that treats one differently
    // from prose asks here rather than keeping its own idea of the pattern.
    static bool isTableRow(const QString &text);

    // The `|---|---|` row. It is scaffolding, so it is drawn as a rule
    // rather than shown.
    static bool isTableSeparator(const QString &text);

    // Where a `*` list marker sits in the line, or -1. The asterisk is
    // hidden and a bullet is drawn in its place.
    static int asteriskBulletColumn(const QString &text);

    // Where a task's mark sits in the line, or -1. A list marker in front of
    // the box is optional: `[ ] label` is a task on its own.
    static int taskMarkColumn(const QString &text);

    // The ``` or ~~~ row itself. It belongs to the code slab, not to the
    // prose around it.
    static bool isFenceLine(const QString &text);

    // Whether a family can draw box-drawing characters that join into
    // continuous rules: it must have the glyphs, and their ink must be at least
    // as tall as the line they sit on. A font whose box glyphs are shorter than
    // its own line spacing draws every diagram in dashes, however monospaced it
    // is. Noto Sans Mono is such a font.
    static bool drawsContinuousBoxes(const QString &family);
    // QFontInfo::fixedPitch() cannot be trusted, so this compares advances.
    static bool isMonospacedFamily(const QString &family);

    // Pin the family used for fenced code and inline code. Empty picks the best
    // available automatically.
    void setCodeFontFamily(const QString &family);
    QStringList codeFamilies() const { return m_codeFamilies; }

    // A step away from the page's own colour rather than a fixed grey, so the
    // code slab keeps the palette's hue whatever the wallpaper does. The fenced
    // slab is drawn behind the editor from QML; inline code uses it as a
    // character background.
    static QColor codeBackgroundFor(const QString &pageBackground, bool darkMode);
    // Inline code sits on top of the page rather than in it, so its chip is
    // brighter where the block slab is darker.
    static QColor inlineCodeBackgroundFor(const QString &pageBackground, bool darkMode);

    // The colour list markers, pipes and fences are drawn in, so anything
    // drawn in their place matches them.
    static QColor markerColorFor(bool darkMode);

    // Block numbers whose fence state changed in the last pass, and clears the
    // list. Opening a fence restates every line under it, and nothing else can
    // tell which those were.
    QList<int> takeRestatedBlocks();

protected:
    void highlightBlock(const QString &text) override;

private slots:
    void refreshSetextHeadings();

private:
    void rebuildFormats();
    bool highlightFencedCode(const QString &text);
    int highlightCode(const QString &text, int languageIndex, int previousState);
    int languageIndexFor(const QString &language);
    bool highlightTableRow(const QString &text);
    bool highlightMarkers(const QString &text);
    void highlightSetextContent(const QString &text);
    void highlightInline(const QString &text);
    void highlightSearch(const QString &text);
    void scheduleSetextRefresh(int blockNumber);
    void applyBlockState(int state);

    bool m_darkMode = true;
    QString m_customBackground;
    QString m_customForeground;
    QString m_customAccent;
    QFont m_formatFont;
    QString m_codeFontFamily;
    QStringList m_codeFamilies;
    QStringList m_tableFamilies;
    QTextCharFormat m_markerFormat;
    QTextCharFormat m_hiddenMarkerFormat;
    // A backtick keeps its cell and paints nothing, so the chip behind a code
    // span has a space of its own either side of the code.
    QTextCharFormat m_codeMarkerFormat;
    // Same metrics as the character it replaces, but painting nothing, so
    // whatever is drawn in its place lands exactly where it sat.
    QTextCharFormat m_invisibleMarkerFormat;
    QTextCharFormat m_headingFormats[6];
    QTextCharFormat m_boldFormat;
    QTextCharFormat m_italicFormat;
    QTextCharFormat m_strikeFormat;
    QTextCharFormat m_codeFormat;
    QTextCharFormat m_codeBlockFormat;
    QHash<CodeSyntaxHighlighter::Token, QTextCharFormat> m_codeTokenFormats;
    std::unique_ptr<CodeSyntaxHighlighter> m_code;
    QStringList m_fenceLanguages;
    QTextCharFormat m_fenceFormat;
    QTextCharFormat m_tableFormat;
    QTextCharFormat m_tablePipeFormat;
    QTextCharFormat m_tableSeparatorFormat;
    QTextCharFormat m_tableHeaderFormat;
    QTextCharFormat m_quoteFormat;
    QTextCharFormat m_linkFormat;
    QList<int> m_pendingSetextBlocks;
    QList<int> m_restatedBlocks;
    int m_activeBlock = -1;
    QSet<int> m_griddedRows;
    int m_revealedFirst = -1;
    int m_revealedLast = -1;
    QString m_searchQuery;
    int m_currentMatchStart = -1;
    QTextCharFormat m_searchFormat;
    QTextCharFormat m_currentSearchFormat;
};
