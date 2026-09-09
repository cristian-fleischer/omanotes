QT += core gui quick testlib
CONFIG += testcase c++17
TEMPLATE = app
TARGET = tst_omanotes

INCLUDEPATH += ../src
SOURCES += \
    tst_omanotes.cpp \
    ../src/backend.cpp \
    ../src/markdownhighlighter.cpp \
    ../src/vaultmodel.cpp \
    ../src/lexillacodehighlighter.cpp
HEADERS += \
    ../src/backend.h \
    ../src/markdownhighlighter.h \
    ../src/vaultmodel.h \
    ../src/codesyntaxhighlighter.h \
    ../src/lexillacodehighlighter.h

include(../lexilla.pri)

QT += widgets printsupport quickcontrols2 quickdialogs2 dbus
