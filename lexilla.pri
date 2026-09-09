# Lexilla, vendored and linked statically. See third_party/lexilla/README.md.
LEXILLA = $$PWD/third_party/lexilla

INCLUDEPATH += $$LEXILLA/include $$LEXILLA/lexlib

LEXILLA_SOURCES = \
    $$files($$LEXILLA/lexlib/*.cxx) \
    $$LEXILLA/lexers/LexHTML.cxx \
    $$LEXILLA/lexers/LexCPP.cxx \
    $$LEXILLA/lexers/LexBash.cxx \
    $$LEXILLA/lexers/LexJSON.cxx \
    $$LEXILLA/lexers/LexPython.cxx \
    $$LEXILLA/lexers/LexSQL.cxx \
    $$LEXILLA/lexers/LexYAML.cxx \
    $$LEXILLA/lexers/LexProps.cxx

# Compiled for size rather than speed, through a compiler of its own so the
# flag does not reach the rest of the app. Nine lexers are 380 KB of the binary
# at -O2 and 150 KB at -Os, and none of it is on a hot path: a lexer runs over
# the lines of a fenced block, not over the document. Third-party source, so it
# is also not held to this project's warning settings.
lexilla.name = lexilla
lexilla.input = LEXILLA_SOURCES
lexilla.output = ${QMAKE_FILE_BASE}.o
lexilla.commands = $$QMAKE_CXX -c $(CXXFLAGS) -Os $$QMAKE_CXXFLAGS_SPLIT_SECTIONS \
    -Wno-deprecated-declarations $(INCPATH) -o ${QMAKE_FILE_OUT} ${QMAKE_FILE_IN}
lexilla.variable_out = OBJECTS
lexilla.CONFIG += no_link explicit_dependencies
QMAKE_EXTRA_COMPILERS += lexilla
