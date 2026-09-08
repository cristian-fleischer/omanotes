#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// What a fenced code block is coloured with.
//
// The Markdown highlighter knows only these token kinds and never sees a lexer.
// A backend maps its own style numbers onto them, so replacing the backend
// touches one file and leaves the palette, the formats and the fenced-code path
// alone.
class CodeSyntaxHighlighter {
public:
    enum class Token {
        Default,
        Keyword,
        Type,
        String,
        Number,
        Comment,
        Operator,
        Preprocessor,
        Identifier,
        Tag,
        Attribute,
        Error,
    };

    struct Span {
        int start = 0;
        int length = 0;
        Token token = Token::Default;
    };

    virtual ~CodeSyntaxHighlighter();

    // Languages the backend can colour, lowercased, aliases included. Used to
    // decide whether a fence's info string means anything.
    virtual QStringList languages() const = 0;
    virtual bool supports(const QString &language) const;

    // One line at a time, because that is how QSyntaxHighlighter works.
    // `state` carries the lexer across lines: the caller keeps it on the block
    // and hands back the previous line's value, zero for the first line. Spans
    // are returned in order and cover only what is not Default.
    virtual QList<Span> tokenize(const QString &language, const QString &line,
                                 int &state) = 0;
};
