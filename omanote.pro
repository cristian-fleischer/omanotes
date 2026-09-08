QT += core gui widgets printsupport qml quick quickcontrols2 quickdialogs2 dbus

CONFIG += c++17 release
TARGET = omanote
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/markdownhighlighter.h \
    src/systemtheme.h \
    src/vaultmodel.h \
    src/codesyntaxhighlighter.h \
    src/lexillacodehighlighter.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/markdownhighlighter.cpp \
    src/systemtheme.cpp \
    src/vaultmodel.cpp \
    src/lexillacodehighlighter.cpp

include(lexilla.pri)

RESOURCES += src/resources.qrc
