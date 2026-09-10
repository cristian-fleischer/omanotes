#include <QFont>
#include <QFontDatabase>
#include <QApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QUrl>
#include <QWindow>
#include <QFile>

#include "backend.h"
#include "systemfonts.h"
#include "systemtheme.h"
#include "vaultmodel.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("omanotes"));
    app.setDesktopFileName(QStringLiteral("omanotes"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("omanotes")));

    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/JetBrainsMonoNL-Regular.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/JetBrainsMonoNL-Italic.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/JetBrainsMonoNL-Bold.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/JetBrainsMonoNL-BoldItalic.ttf"));
    // QSettings resolves to ~/.config/omanotes/omanotes.conf. Leaving the
    // organisation domain unset keeps the path off omacom.io, so Omanotes
    // and OmaWrite never share a settings file.
    app.setOrganizationName(QStringLiteral("omanotes"));

    QQuickStyle::setStyle(QStringLiteral("Material"));

    Backend backend(&app);
    VaultModel vault(&app);
    // The root comes from settings, never from a path baked into the binary.
    vault.loadSettings();

    SystemTheme systemTheme(&app);
    backend.setDarkMode(systemTheme.darkMode());
    QObject::connect(&systemTheme, &SystemTheme::darkModeChanged, &backend,
                     &Backend::setDarkMode);

    // Carry the desktop's text scale into the default font, so the chrome that
    // inherits it (dialog titles, buttons) grows along with the writing area.
    // view/interfaceFontFamily first, then whatever the desktop is set to use
    // for its own interface, then the font in the binary. The chrome follows
    // the desktop rather than the writing surface, so this one is proportional
    // where it can be.
    QString interfaceFamily = Backend::interfaceFontFamily();
    if (interfaceFamily.isEmpty())
        interfaceFamily = SystemFonts::interfaceFamily();
    if (interfaceFamily.isEmpty())
        interfaceFamily = SystemFonts::bundledFamily();
    const QFont interfaceFont(interfaceFamily);
    const qreal basePointSize = interfaceFont.pointSizeF() > 0
        ? interfaceFont.pointSizeF()
        : app.font().pointSizeF();
    const auto applyInterfaceFont = [&app, interfaceFont, basePointSize](qreal textScale) {
        QFont scaled = interfaceFont;
        scaled.setPointSizeF(basePointSize * textScale);
        app.setFont(scaled);
    };
    applyInterfaceFont(systemTheme.textScale());

    backend.setTextScale(systemTheme.textScale());
    QObject::connect(&systemTheme, &SystemTheme::textScaleChanged, &backend,
                     [&backend, applyInterfaceFont](qreal textScale) {
        applyInterfaceFont(textScale);
        backend.setTextScale(textScale);
    });

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
                     [](const QList<QQmlError> &warnings) {
        for (const QQmlError &warning : warnings)
            qWarning().noquote() << warning.toString();
    });
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("vault"), &vault);

    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        qCritical() << "Could not load the Omanotes interface; resource available:"
                    << QFile::exists(QStringLiteral(":/Main.qml"));
        return -1;
    }

    backend.setParentWindow(qobject_cast<QWindow *>(engine.rootObjects().constFirst()));

    // A file named on the command line wins over whatever the last session
    // left behind: open() stashes the restored draft under the note it belongs
    // to, so nothing is lost by moving off it.
    const QStringList args = app.arguments();
    if (args.size() > 1)
        backend.open(QUrl::fromLocalFile(args.at(1)));

    return app.exec();
}
