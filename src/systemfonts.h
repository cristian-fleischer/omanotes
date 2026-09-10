#pragma once

#include <QString>

// What the desktop is configured to use, asked of the desktop rather than
// guessed at. KDE writes its choice to kdeglobals and Qt's own platform theme
// reads the same file; GNOME keeps its own in gsettings; fontconfig has the
// last word for everything else, which is what a tiling window manager with no
// desktop settings at all ends up with.
//
// Both answers are worked out once and remembered: reading kdeglobals is a file
// read and asking gsettings costs one short-lived process.
namespace SystemFonts {

// The proportional font the desktop uses for its own interface. Empty when
// nothing readable is configured.
QString interfaceFamily();

// The monospace font the desktop is configured with. Empty as above.
QString fixedFamily();

// The font compiled into the binary, which is always there.
QString bundledFamily();

// The family out of a font description, in whichever syntax wrote it: Qt's
// "Noto Sans,10,-1,5,50,0,0,0,0,0" or GNOME's "'Noto Sans Bold 10'". Exposed
// so the parsing can be tested without a desktop to read it from.
QString familyFromDescription(QString description);

// [General] font= or fixed= out of a kdeglobals-shaped file. Exposed for the
// same reason.
QString familyFromKdeglobals(const QString &path, const QString &key);

}
