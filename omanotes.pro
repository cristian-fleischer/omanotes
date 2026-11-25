QT += core gui widgets printsupport qml quick quickcontrols2 dbus

# For size: 170 KB less than -O2, and neither a launch, a keystroke nor opening a
# long note is measurably slower. What takes time is in Qt.
CONFIG += c++17 release optimize_size
TARGET = omanotes
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/markdownhighlighter.h \
    src/portalsettings.h \
    src/systemfonts.h \
    src/systemtheme.h \
    src/vaultmodel.h \
    src/warmup.h \
    src/codesyntaxhighlighter.h \
    src/lexillacodehighlighter.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/markdownhighlighter.cpp \
    src/portalsettings.cpp \
    src/systemfonts.cpp \
    src/systemtheme.cpp \
    src/vaultmodel.cpp \
    src/warmup.cpp \
    src/lexillacodehighlighter.cpp

include(lexilla.pri)

RESOURCES += src/resources.qrc

# rcc leaves a file uncompressed unless compression saves 70 per cent, so ask for
# every file to be tried. The four font faces are one collection because most of
# what they hold is the same: zstd's window reaches from one face to the next and
# brings 508 KB to 138 KB, where each face on its own came to 66 KB. Qt unpacks
# it on access, which is when the font is used.
qtConfig(zstd): QMAKE_RESOURCE_FLAGS += -threshold 0 -compress-algo zstd -compress 19
else: QMAKE_RESOURCE_FLAGS += -threshold 0 -compress 9

# Link-time optimisation and section garbage collection, together worth about
# 300 KB of the binary: Lexilla brings nine lexers of which any one document
# uses one, and gc-sections drops what nothing calls.
QMAKE_CXXFLAGS_SPLIT_SECTIONS = -ffunction-sections -fdata-sections
QMAKE_CXXFLAGS += $$QMAKE_CXXFLAGS_SPLIT_SECTIONS -flto=auto
QMAKE_LFLAGS += -Wl,--gc-sections -flto=auto -Os

# Relocations in the packed form, 37 KB less of them. Needs a linker from 2022.
system("ld --help 2>/dev/null | grep -q pack-relative-relocs"): \
    QMAKE_LFLAGS += -Wl,-z,pack-relative-relocs
