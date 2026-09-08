#pragma once

#include <QList>
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

    // Carried on the block's user state so the next block knows whether it is
    // inside a fence, and so Backend::hiddenRangesAt can tell that a line of
    // code is not a line of Markdown.
    enum BlockState { Normal = 0, InFencedCode = 1 };

    struct Span {
        int start;
        int length;
    };

    enum class InlineKind { Bold, Italic, Strikethrough, Link, Image, Heading };

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

protected:
    void highlightBlock(const QString &text) override;

private slots:
    void refreshSetextHeadings();

private:
    void rebuildFormats();
    bool highlightFencedCode(const QString &text);
    bool highlightTableRow(const QString &text);
    void highlightMarkers(const QString &text);
    void highlightSetextContent(const QString &text);
    void highlightInline(const QString &text);
    void highlightSearch(const QString &text);
    void scheduleSetextRefresh(int blockNumber);

    bool m_darkMode = true;
    QString m_customBackground;
    QString m_customForeground;
    QString m_customAccent;
    QFont m_formatFont;
    QStringList m_monospaceFamilies;
    QTextCharFormat m_markerFormat;
    QTextCharFormat m_hiddenMarkerFormat;
    QTextCharFormat m_headingFormats[6];
    QTextCharFormat m_boldFormat;
    QTextCharFormat m_italicFormat;
    QTextCharFormat m_strikeFormat;
    QTextCharFormat m_codeFormat;
    QTextCharFormat m_codeBlockFormat;
    QTextCharFormat m_fenceFormat;
    QTextCharFormat m_tableFormat;
    QTextCharFormat m_tablePipeFormat;
    QTextCharFormat m_tableSeparatorFormat;
    QTextCharFormat m_quoteFormat;
    QTextCharFormat m_linkFormat;
    QList<int> m_pendingSetextBlocks;
    QString m_searchQuery;
    int m_currentMatchStart = -1;
    QTextCharFormat m_searchFormat;
    QTextCharFormat m_currentSearchFormat;
};
