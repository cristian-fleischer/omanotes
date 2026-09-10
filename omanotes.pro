QT += core gui widgets printsupport qml quick quickcontrols2 quickdialogs2 dbus

CONFIG += c++17 release
TARGET = omanotes
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/markdownhighlighter.h \
    src/systemfonts.h \
    src/systemtheme.h \
    src/vaultmodel.h \
    src/codesyntaxhighlighter.h \
    src/lexillacodehighlighter.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/markdownhighlighter.cpp \
    src/systemfonts.cpp \
    src/systemtheme.cpp \
    src/vaultmodel.cpp \
    src/lexillacodehighlighter.cpp

include(lexilla.pri)

RESOURCES += src/resources.qrc

# rcc leaves a file uncompressed unless compression saves 70 per cent, and the
# four font faces only halve. Halving 400 KB is worth having, so ask for every
# file to be tried. Qt decompresses on access, which costs about 2 ms at startup.
QMAKE_RESOURCE_FLAGS += -threshold 0 -compress 9

# Link-time optimisation and section garbage collection, together worth about
# 300 KB of the binary: Lexilla brings nine lexers of which any one document
# uses one, and gc-sections drops what nothing calls.
QMAKE_CXXFLAGS_SPLIT_SECTIONS = -ffunction-sections -fdata-sections
QMAKE_CXXFLAGS += $$QMAKE_CXXFLAGS_SPLIT_SECTIONS -flto=auto
QMAKE_LFLAGS += -Wl,--gc-sections -flto=auto -O2
