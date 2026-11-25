#pragma once

#include <QString>
#include <QVariant>

// The desktop's settings, asked of the settings portal. Invalid when the portal
// is missing, slow to answer, or does not know the key; a short timeout keeps a
// stalled portal from holding up the GUI thread.
namespace PortalSettings {

// What the portal says now.
QVariant read(const QString &nameSpace, const QString &key);

// What it said when the app started. Everything a launch needs to know is asked
// for in one round trip, the first time any of it is wanted, and remembered.
QVariant atStartup(const QString &nameSpace, const QString &key);

}
