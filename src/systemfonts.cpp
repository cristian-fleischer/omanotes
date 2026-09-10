#include "systemfonts.h"

#include <QFile>
#include <QFontDatabase>
#include <QProcess>
#include <QRawFont>
#include <QMetaType>
#include <QSettings>
#include <QVariant>
#include <QStandardPaths>
#include <QStringList>
#include <utility>

namespace {

bool isInstalled(const QString &family) {
    return !family.isEmpty()
        && QFontDatabase::families().contains(family, Qt::CaseInsensitive);
}

// A font description carries the family and then how to draw it. Qt writes
// "Noto Sans,10,-1,5,50,0,0,0,0,0"; GNOME writes "Noto Sans Bold 10". Take the
// family and leave the rest: the app decides its own sizes and weights.
QString familyOfImpl(QString description) {
    description = description.trimmed();
    if (description.startsWith(QLatin1Char('\'')) && description.endsWith(QLatin1Char('\'')))
        description = description.mid(1, description.size() - 2);
    if (description.contains(QLatin1Char(',')))
        return description.section(QLatin1Char(','), 0, 0).trimmed();

    static const QStringList styles{
        QStringLiteral("thin"), QStringLiteral("extralight"), QStringLiteral("ultralight"),
        QStringLiteral("light"), QStringLiteral("regular"), QStringLiteral("book"),
        QStringLiteral("medium"), QStringLiteral("semibold"), QStringLiteral("demibold"),
        QStringLiteral("bold"), QStringLiteral("extrabold"), QStringLiteral("black"),
        QStringLiteral("heavy"), QStringLiteral("italic"), QStringLiteral("oblique"),
        QStringLiteral("condensed"), QStringLiteral("expanded")};

    QStringList words = description.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    while (!words.isEmpty()) {
        bool numeric = false;
        words.constLast().toDouble(&numeric);
        if (!numeric && !styles.contains(words.constLast().toLower()))
            break;
        words.removeLast();
    }
    return words.join(QLatin1Char(' '));
}

// [General] font= and fixed=, which is where Plasma keeps them and where Qt's
// own KDE platform theme looks.
QString fromKde(const QString &key) {
    return SystemFonts::familyFromKdeglobals(
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QStringLiteral("/kdeglobals"),
        key);
}

// org.gnome.desktop.interface. Reading dconf means either linking glib or
// parsing its database, so ask the tool that ships with it. It answers in about
// three milliseconds and is asked at most twice in the life of the process.
QString fromGnome(const QString &key) {
    if (QStandardPaths::findExecutable(QStringLiteral("gsettings")).isEmpty())
        return {};
    QProcess gsettings;
    gsettings.start(QStringLiteral("gsettings"),
                    {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.interface"), key});
    if (!gsettings.waitForFinished(500)) {
        gsettings.kill();
        gsettings.waitForFinished(100);
        return {};
    }
    if (gsettings.exitStatus() != QProcess::NormalExit || gsettings.exitCode() != 0)
        return {};
    return SystemFonts::familyFromDescription(
        QString::fromUtf8(gsettings.readAllStandardOutput()).trimmed());
}

// systemFont() hands back whatever the active platform theme says, and the
// alias "monospace" when that is fontconfig. QTextCharFormat cannot resolve an
// alias, so ask which family it actually resolved to.
QString fromQt(QFontDatabase::SystemFont role) {
    const QFont font = QFontDatabase::systemFont(role);
    const QString resolved = QRawFont::fromFont(font).familyName();
    return resolved.isEmpty() ? font.family() : resolved;
}

QString firstInstalled(const QStringList &candidates) {
    for (const QString &candidate : candidates) {
        if (isInstalled(candidate))
            return candidate;
    }
    return {};
}

} // namespace

QString SystemFonts::familyFromDescription(QString description) {
    return familyOfImpl(std::move(description));
}

QString SystemFonts::familyFromKdeglobals(const QString &path, const QString &key) {
    if (!QFile::exists(path))
        return {};
    QSettings settings(path, QSettings::IniFormat);
    // QSettings folds an INI file's [General] section into the root, because
    // that is where it puts ungrouped keys of its own. So the key kdeglobals
    // writes under [General] is read without a group; the qualified name is
    // tried as well in case a future Qt stops doing that.
    QVariant value = settings.value(key);
    if (!value.isValid())
        value = settings.value(QStringLiteral("General/") + key);
    // A Qt font description is commas all the way down, and QSettings reads an
    // unquoted comma-separated value as a list. Put it back together.
    return familyOfImpl(value.typeId() == QMetaType::QStringList
                            ? value.toStringList().join(QLatin1Char(','))
                            : value.toString());
}

QString SystemFonts::interfaceFamily() {
    static const QString family = firstInstalled({fromKde(QStringLiteral("font")),
                                                  fromGnome(QStringLiteral("font-name")),
                                                  fromQt(QFontDatabase::GeneralFont)});
    return family;
}

QString SystemFonts::fixedFamily() {
    static const QString family = firstInstalled({fromKde(QStringLiteral("fixed")),
                                                  fromGnome(QStringLiteral("monospace-font-name")),
                                                  fromQt(QFontDatabase::FixedFont)});
    return family;
}

QString SystemFonts::bundledFamily() {
    return QStringLiteral("JetBrains Mono NL");
}
