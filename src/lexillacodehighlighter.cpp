#include "lexillacodehighlighter.h"

#include <QByteArray>

#include "ILexer.h"
#include "LexerModule.h"
#include "SciLexer.h"

using Scintilla::IDocument;
using Scintilla::ILexer5;

namespace {

// Which SCE_* numbering a lexer speaks. The numbers overlap between lexers, so
// the mapping to tokens has to know which family it is reading.
enum Family { FamilyCpp, FamilyPhp, FamilyBash, FamilyPython, FamilySql, FamilyJson,
              FamilyYaml, FamilyProps };

struct Language {
    const char *name;
    const char *module;   // Lexilla module name, as registered by the lexer
    Family family;
    // Which of the lexer's word lists the keywords belong in. Zero for most,
    // but LexHTML numbers them by embedded language: 1 is JavaScript, 4 is PHP.
    int keywordList;
    const char *keywords;
};

// Keyword lists are the lexer's, not ours: Scintilla lexers colour nothing as a
// keyword until the host supplies them.
const char *const cppKeywords =
    "break case catch class const continue default delete do else enum export extends "
    "false finally for function if import in instanceof let new null of return static "
    "super switch this throw true try typeof var void while yield async await "
    "int long float double char bool auto struct union namespace template typename "
    "public private protected virtual override final inline sizeof nullptr constexpr "
    "package interface implements abstract synchronized func go defer chan map range "
    "fn impl mut pub trait use crate match loop where";

const char *const phpKeywords =
    "abstract and array as break callable case catch class clone const continue declare "
    "default do echo else elseif empty enddeclare endfor endforeach endif endswitch "
    "endwhile enum extends final finally fn for foreach function global goto if "
    "implements include include_once instanceof insteadof interface isset list match "
    "namespace new or print private protected public readonly require require_once "
    "return static switch throw trait try unset use var while xor yield "
    "true false null int float string bool void iterable object self parent";

const char *const bashKeywords =
    "alias bg bind break builtin caller case cd command compgen complete continue "
    "declare dirs disown do done echo elif else esac eval exec exit export false fc fg "
    "fi for function getopts hash help history if in jobs kill let local logout popd "
    "printf pushd pwd read readonly return select set shift shopt source suspend test "
    "then time times trap true type typeset ulimit umask unalias unset until wait while";

const char *const pythonKeywords =
    "and as assert async await break class continue def del elif else except False "
    "finally for from global if import in is lambda None nonlocal not or pass raise "
    "return True try while with yield match case self";

const char *const sqlKeywords =
    "add all alter and any as asc between by case cast check column constraint create "
    "cross database default delete desc distinct drop else end exists foreign from full "
    "group having if in index inner insert into is join key left like limit not null on "
    "or order outer primary references right select set table then union unique update "
    "values view when where with";

const Language languages[] = {
    {"php", "phpscript", FamilyPhp, 4, phpKeywords},
    {"blade", "phpscript", FamilyPhp, 4, phpKeywords},
    {"javascript", "cpp", FamilyCpp, 0, cppKeywords},
    {"js", "cpp", FamilyCpp, 0, cppKeywords},
    {"jsx", "cpp", FamilyCpp, 0, cppKeywords},
    {"typescript", "cpp", FamilyCpp, 0, cppKeywords},
    {"ts", "cpp", FamilyCpp, 0, cppKeywords},
    {"c", "cpp", FamilyCpp, 0, cppKeywords},
    {"cpp", "cpp", FamilyCpp, 0, cppKeywords},
    {"c++", "cpp", FamilyCpp, 0, cppKeywords},
    {"java", "cpp", FamilyCpp, 0, cppKeywords},
    {"cs", "cpp", FamilyCpp, 0, cppKeywords},
    {"go", "cpp", FamilyCpp, 0, cppKeywords},
    {"rust", "cpp", FamilyCpp, 0, cppKeywords},
    {"rs", "cpp", FamilyCpp, 0, cppKeywords},
    {"json", "json", FamilyJson, 0, "true false null"},
    {"bash", "bash", FamilyBash, 0, bashKeywords},
    {"sh", "bash", FamilyBash, 0, bashKeywords},
    {"shell", "bash", FamilyBash, 0, bashKeywords},
    {"zsh", "bash", FamilyBash, 0, bashKeywords},
    {"python", "python", FamilyPython, 0, pythonKeywords},
    {"py", "python", FamilyPython, 0, pythonKeywords},
    {"sql", "sql", FamilySql, 0, sqlKeywords},
    {"yaml", "yaml", FamilyYaml, 0, "true false null yes no on off"},
    {"yml", "yaml", FamilyYaml, 0, "true false null yes no on off"},
    // A .env file is a properties file: KEY=value with # comments. So are the
    // ini and conf files that turn up in notes next to them.
    {"env", "props", FamilyProps, 0, ""},
    {"dotenv", "props", FamilyProps, 0, ""},
    {"ini", "props", FamilyProps, 0, ""},
    {"conf", "props", FamilyProps, 0, ""},
    {"properties", "props", FamilyProps, 0, ""},
    {"toml", "props", FamilyProps, 0, ""},
};

using Token = CodeSyntaxHighlighter::Token;

Token tokenForCpp(int style) {
    switch (style) {
    case SCE_C_COMMENT: case SCE_C_COMMENTLINE: case SCE_C_COMMENTDOC:
    case SCE_C_COMMENTLINEDOC: case SCE_C_COMMENTDOCKEYWORD:
    case SCE_C_COMMENTDOCKEYWORDERROR:
        return Token::Comment;
    case SCE_C_NUMBER:
        return Token::Number;
    case SCE_C_WORD:
        return Token::Keyword;
    case SCE_C_WORD2:
        return Token::Type;
    case SCE_C_STRING: case SCE_C_CHARACTER: case SCE_C_STRINGEOL:
    case SCE_C_VERBATIM: case SCE_C_TRIPLEVERBATIM: case SCE_C_STRINGRAW:
    case SCE_C_HASHQUOTEDSTRING:
        return Token::String;
    case SCE_C_PREPROCESSOR: case SCE_C_PREPROCESSORCOMMENT:
    case SCE_C_PREPROCESSORCOMMENTDOC:
        return Token::Preprocessor;
    case SCE_C_OPERATOR:
        return Token::Operator;
    case SCE_C_IDENTIFIER:
        return Token::Identifier;
    default:
        return Token::Default;
    }
}

Token tokenForPhp(int style) {
    switch (style) {
    case SCE_HPHP_COMMENT: case SCE_HPHP_COMMENTLINE:
        return Token::Comment;
    case SCE_HPHP_NUMBER:
        return Token::Number;
    case SCE_HPHP_WORD:
        return Token::Keyword;
    case SCE_HPHP_HSTRING: case SCE_HPHP_SIMPLESTRING:
    case SCE_HPHP_HSTRING_VARIABLE: case SCE_HPHP_COMPLEX_VARIABLE:
        return Token::String;
    case SCE_HPHP_VARIABLE:
        return Token::Identifier;
    case SCE_HPHP_OPERATOR:
        return Token::Operator;
    case SCE_H_TAG: case SCE_H_TAGUNKNOWN: case SCE_H_TAGEND: case SCE_H_XMLSTART:
    case SCE_H_XMLEND:
        return Token::Tag;
    case SCE_H_ATTRIBUTE: case SCE_H_ATTRIBUTEUNKNOWN:
        return Token::Attribute;
    case SCE_H_DOUBLESTRING: case SCE_H_SINGLESTRING:
        return Token::String;
    case SCE_H_COMMENT:
        return Token::Comment;
    case SCE_H_NUMBER:
        return Token::Number;
    default:
        return Token::Default;
    }
}

Token tokenForBash(int style) {
    switch (style) {
    case SCE_SH_COMMENTLINE:
        return Token::Comment;
    case SCE_SH_NUMBER:
        return Token::Number;
    case SCE_SH_WORD:
        return Token::Keyword;
    case SCE_SH_STRING: case SCE_SH_CHARACTER: case SCE_SH_BACKTICKS:
    case SCE_SH_HERE_DELIM: case SCE_SH_HERE_Q:
        return Token::String;
    case SCE_SH_SCALAR: case SCE_SH_PARAM:
        return Token::Identifier;
    case SCE_SH_OPERATOR:
        return Token::Operator;
    case SCE_SH_ERROR:
        return Token::Error;
    default:
        return Token::Default;
    }
}

Token tokenForPython(int style) {
    switch (style) {
    case SCE_P_COMMENTLINE: case SCE_P_COMMENTBLOCK:
        return Token::Comment;
    case SCE_P_NUMBER:
        return Token::Number;
    case SCE_P_WORD:
        return Token::Keyword;
    case SCE_P_WORD2:
        return Token::Type;
    case SCE_P_STRING: case SCE_P_CHARACTER: case SCE_P_TRIPLE:
    case SCE_P_TRIPLEDOUBLE: case SCE_P_FSTRING: case SCE_P_FCHARACTER:
    case SCE_P_FTRIPLE: case SCE_P_FTRIPLEDOUBLE:
        return Token::String;
    case SCE_P_DEFNAME: case SCE_P_CLASSNAME:
        return Token::Type;
    case SCE_P_DECORATOR:
        return Token::Preprocessor;
    case SCE_P_OPERATOR:
        return Token::Operator;
    case SCE_P_IDENTIFIER:
        return Token::Identifier;
    default:
        return Token::Default;
    }
}

Token tokenForSql(int style) {
    switch (style) {
    case SCE_SQL_COMMENT: case SCE_SQL_COMMENTLINE: case SCE_SQL_COMMENTDOC:
    case SCE_SQL_COMMENTLINEDOC:
        return Token::Comment;
    case SCE_SQL_NUMBER:
        return Token::Number;
    case SCE_SQL_WORD: case SCE_SQL_WORD2:
        return Token::Keyword;
    case SCE_SQL_STRING: case SCE_SQL_CHARACTER:
        return Token::String;
    case SCE_SQL_OPERATOR:
        return Token::Operator;
    case SCE_SQL_IDENTIFIER:
        return Token::Identifier;
    default:
        return Token::Default;
    }
}

Token tokenForJson(int style) {
    switch (style) {
    case SCE_JSON_LINECOMMENT: case SCE_JSON_BLOCKCOMMENT:
        return Token::Comment;
    case SCE_JSON_NUMBER:
        return Token::Number;
    case SCE_JSON_STRING: case SCE_JSON_STRINGEOL:
        return Token::String;
    case SCE_JSON_PROPERTYNAME:
        return Token::Attribute;
    case SCE_JSON_KEYWORD:
        return Token::Keyword;
    case SCE_JSON_OPERATOR:
        return Token::Operator;
    case SCE_JSON_ERROR:
        return Token::Error;
    default:
        return Token::Default;
    }
}

Token tokenForYaml(int style) {
    switch (style) {
    case SCE_YAML_COMMENT:
        return Token::Comment;
    case SCE_YAML_NUMBER:
        return Token::Number;
    case SCE_YAML_KEYWORD:
        return Token::Keyword;
    case SCE_YAML_IDENTIFIER:
        return Token::Attribute;
    case SCE_YAML_REFERENCE: case SCE_YAML_DOCUMENT:
        return Token::Preprocessor;
    case SCE_YAML_TEXT:
        return Token::String;
    case SCE_YAML_OPERATOR:
        return Token::Operator;
    case SCE_YAML_ERROR:
        return Token::Error;
    default:
        return Token::Default;
    }
}

Token tokenForProps(int style) {
    switch (style) {
    case SCE_PROPS_COMMENT:
        return Token::Comment;
    case SCE_PROPS_SECTION:
        return Token::Keyword;
    case SCE_PROPS_KEY:
        return Token::Attribute;
    case SCE_PROPS_ASSIGNMENT:
        return Token::Operator;
    case SCE_PROPS_DEFVAL:
        return Token::String;
    default:
        return Token::Default;
    }
}

Token tokenFor(int family, int style) {
    switch (family) {
    case FamilyCpp: return tokenForCpp(style);
    case FamilyPhp: return tokenForPhp(style);
    case FamilyBash: return tokenForBash(style);
    case FamilyPython: return tokenForPython(style);
    case FamilySql: return tokenForSql(style);
    case FamilyJson: return tokenForJson(style);
    case FamilyYaml: return tokenForYaml(style);
    case FamilyProps: return tokenForProps(style);
    default: return Token::Default;
    }
}

// A Scintilla document holding exactly one line. Lexilla lexers read forward
// through this and write a style byte per character; carrying the end state
// between calls is what makes block comments and here-documents work.
class LineDocument final : public IDocument {
public:
    explicit LineDocument(const QByteArray &line)
        : m_text(line), m_styles(line.size(), 0) {}

    const QByteArray &styles() const { return m_styles; }
    int endState() const { return m_lineState; }
    void setStartState(int state) { m_lineState = state; }

    int SCI_METHOD Version() const override { return Scintilla::dvRelease4; }
    void SCI_METHOD SetErrorStatus(int) override {}
    Sci_Position SCI_METHOD Length() const override { return m_text.size(); }

    void SCI_METHOD GetCharRange(char *buffer, Sci_Position position,
                                 Sci_Position lengthRetrieve) const override {
        if (position < 0 || lengthRetrieve <= 0)
            return;
        const Sci_Position available = qMin(lengthRetrieve, m_text.size() - position);
        if (available > 0)
            memcpy(buffer, m_text.constData() + position, size_t(available));
        for (Sci_Position i = available; i < lengthRetrieve; ++i)
            buffer[i] = '\0';
    }

    char SCI_METHOD StyleAt(Sci_Position position) const override {
        return position >= 0 && position < m_styles.size() ? m_styles.at(position) : 0;
    }

    Sci_Position SCI_METHOD LineFromPosition(Sci_Position) const override { return 0; }
    Sci_Position SCI_METHOD LineStart(Sci_Position line) const override {
        return line <= 0 ? 0 : m_text.size();
    }
    int SCI_METHOD GetLevel(Sci_Position) const override { return 0x400; }
    int SCI_METHOD SetLevel(Sci_Position, int level) override { return level; }
    int SCI_METHOD GetLineState(Sci_Position) const override { return m_lineState; }
    int SCI_METHOD SetLineState(Sci_Position, int state) override {
        const int previous = m_lineState;
        m_lineState = state;
        return previous;
    }

    void SCI_METHOD StartStyling(Sci_Position position) override { m_stylingAt = position; }

    bool SCI_METHOD SetStyleFor(Sci_Position length, char style) override {
        for (Sci_Position i = 0; i < length; ++i) {
            if (m_stylingAt >= 0 && m_stylingAt < m_styles.size())
                m_styles[m_stylingAt] = style;
            ++m_stylingAt;
        }
        return true;
    }

    bool SCI_METHOD SetStyles(Sci_Position length, const char *styles) override {
        for (Sci_Position i = 0; i < length; ++i) {
            if (m_stylingAt >= 0 && m_stylingAt < m_styles.size())
                m_styles[m_stylingAt] = styles[i];
            ++m_stylingAt;
        }
        return true;
    }

    void SCI_METHOD DecorationSetCurrentIndicator(int) override {}
    void SCI_METHOD DecorationFillRange(Sci_Position, int, Sci_Position) override {}
    void SCI_METHOD ChangeLexerState(Sci_Position, Sci_Position) override {}
    int SCI_METHOD CodePage() const override { return 65001; }
    bool SCI_METHOD IsDBCSLeadByte(char) const override { return false; }
    const char * SCI_METHOD BufferPointer() override { return m_text.constData(); }
    int SCI_METHOD GetLineIndentation(Sci_Position) override { return 0; }

    Sci_Position SCI_METHOD LineEnd(Sci_Position) const override { return m_text.size(); }

    Sci_Position SCI_METHOD GetRelativePosition(Sci_Position positionStart,
                                                Sci_Position characterOffset) const override {
        return qBound(Sci_Position(0), positionStart + characterOffset, m_text.size());
    }

    int SCI_METHOD GetCharacterAndWidth(Sci_Position position,
                                        Sci_Position *pWidth) const override {
        if (pWidth)
            *pWidth = 1;
        return position >= 0 && position < m_text.size()
            ? static_cast<unsigned char>(m_text.at(position))
            : 0;
    }

private:
    QByteArray m_text;
    QByteArray m_styles;
    Sci_Position m_stylingAt = 0;
    int m_lineState = 0;
};

} // namespace

// Lexilla registers each lexer as a LexerModule with external linkage. Naming
// them here rather than going through Lexilla's CreateLexer catalogue is what
// keeps the other 119 lexers out of the binary.
extern const Lexilla::LexerModule lmPHPSCRIPT;
extern const Lexilla::LexerModule lmCPP;
extern const Lexilla::LexerModule lmBash;
extern const Lexilla::LexerModule lmJSON;
extern const Lexilla::LexerModule lmPython;
extern const Lexilla::LexerModule lmSQL;
extern const Lexilla::LexerModule lmYAML;
extern const Lexilla::LexerModule lmProps;

namespace {

const Lexilla::LexerModule *moduleNamed(const char *name) {
    const QLatin1String wanted(name);
    if (wanted == QLatin1String("phpscript")) return &lmPHPSCRIPT;
    if (wanted == QLatin1String("cpp")) return &lmCPP;
    if (wanted == QLatin1String("bash")) return &lmBash;
    if (wanted == QLatin1String("json")) return &lmJSON;
    if (wanted == QLatin1String("python")) return &lmPython;
    if (wanted == QLatin1String("sql")) return &lmSQL;
    if (wanted == QLatin1String("yaml")) return &lmYAML;
    if (wanted == QLatin1String("props")) return &lmProps;
    return nullptr;
}

} // namespace

CodeSyntaxHighlighter::~CodeSyntaxHighlighter() = default;

bool CodeSyntaxHighlighter::supports(const QString &language) const {
    return !language.isEmpty() && languages().contains(language.toLower());
}

LexillaCodeHighlighter::LexillaCodeHighlighter() = default;

LexillaCodeHighlighter::~LexillaCodeHighlighter() {
    for (Lexer &lexer : m_lexers) {
        if (lexer.lexer)
            lexer.lexer->Release();
    }
}

QStringList LexillaCodeHighlighter::languages() const {
    QStringList names;
    names.reserve(int(std::size(::languages)));
    for (const Language &language : ::languages)
        names.append(QString::fromLatin1(language.name));
    return names;
}

LexillaCodeHighlighter::Lexer *LexillaCodeHighlighter::lexerFor(const QString &language) {
    const QString key = language.toLower();
    const auto cached = m_lexers.find(key);
    if (cached != m_lexers.end())
        return cached->lexer ? &*cached : nullptr;

    for (const Language &candidate : ::languages) {
        if (key != QLatin1String(candidate.name))
            continue;

        const Lexilla::LexerModule *module = moduleNamed(candidate.module);
        if (!module)
            break;

        Lexer lexer;
        lexer.lexer = module->Create();
        lexer.family = candidate.family;
        if (lexer.lexer)
            lexer.lexer->WordListSet(candidate.keywordList, candidate.keywords);
        const auto inserted = m_lexers.insert(key, lexer);
        return inserted->lexer ? &*inserted : nullptr;
    }

    m_lexers.insert(key, Lexer{});
    return nullptr;
}

QList<CodeSyntaxHighlighter::Span> LexillaCodeHighlighter::tokenize(const QString &language,
                                                                   const QString &line,
                                                                   int &state) {
    QList<Span> spans;
    Lexer *lexer = lexerFor(language);
    if (!lexer || !lexer->lexer || line.isEmpty())
        return spans;

    // Lexers work in bytes. UTF-8 keeps ASCII one byte per character, which is
    // what every token boundary in these languages is made of; anything wider
    // lands inside a span rather than splitting one.
    const QByteArray utf8 = line.toUtf8();
    LineDocument document(utf8);
    document.setStartState(state);

    const int initStyle = state & 0xff;
    lexer->lexer->Lex(0, utf8.size(), initStyle, &document);

    const QByteArray &styles = document.styles();
    // Walk the style bytes into runs, then map byte offsets back to QString
    // indexes so the caller can call setFormat with them.
    int runStart = 0;
    for (int i = 1; i <= styles.size(); ++i) {
        const bool last = i == styles.size();
        if (!last && styles.at(i) == styles.at(runStart))
            continue;

        const Token token = tokenFor(lexer->family, static_cast<unsigned char>(styles.at(runStart)));
        if (token != Token::Default) {
            const int start = QString::fromUtf8(utf8.constData(), runStart).size();
            const int length =
                QString::fromUtf8(utf8.constData() + runStart, i - runStart).size();
            if (length > 0)
                spans.append(Span{start, length, token});
        }
        runStart = i;
    }

    state = styles.isEmpty() ? initStyle
                             : static_cast<unsigned char>(styles.at(styles.size() - 1));
    return spans;
}
