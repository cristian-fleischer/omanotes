#pragma once

#include "codesyntaxhighlighter.h"

#include <QHash>

namespace Scintilla {
class ILexer5;
}

// Lexilla, the lexer library carved out of Scintilla, linked statically from
// third_party/lexilla. Its lexers are line-oriented and carry their state in an
// int, which is the same shape QSyntaxHighlighter works in.
class LexillaCodeHighlighter final : public CodeSyntaxHighlighter {
public:
    LexillaCodeHighlighter();
    ~LexillaCodeHighlighter() override;

    QStringList languages() const override;
    QList<Span> tokenize(const QString &language, const QString &line,
                         int &state) override;

private:
    struct Lexer {
        Scintilla::ILexer5 *lexer = nullptr;
        // Which family of SCE_* style numbers the lexer emits.
        int family = 0;
    };

    Lexer *lexerFor(const QString &language);

    QHash<QString, Lexer> m_lexers;
};
