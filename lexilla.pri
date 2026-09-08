# Lexilla, vendored and linked statically. See third_party/lexilla/README.md.
LEXILLA = $$PWD/third_party/lexilla

INCLUDEPATH += $$LEXILLA/include $$LEXILLA/lexlib

SOURCES += \
    $$files($$LEXILLA/lexlib/*.cxx) \
    $$LEXILLA/lexers/LexHTML.cxx \
    $$LEXILLA/lexers/LexCPP.cxx \
    $$LEXILLA/lexers/LexBash.cxx \
    $$LEXILLA/lexers/LexJSON.cxx \
    $$LEXILLA/lexers/LexPython.cxx \
    $$LEXILLA/lexers/LexSQL.cxx

# Third-party source: do not hold it to this project's warning settings.
QMAKE_CXXFLAGS += -Wno-deprecated-declarations
