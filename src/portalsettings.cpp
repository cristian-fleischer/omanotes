#include "portalsettings.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDBusVariant>
#include <QMap>
#include <QStringList>
#include <QVariantMap>

namespace {

constexpr int timeoutMs = 150;

QDBusMessage request(const QString &method) {
    return QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.Settings"), method);
}

QMap<QString, QVariantMap> readAll() {
    QMap<QString, QVariantMap> settings;
    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return settings;

    QDBusMessage message = request(QStringLiteral("ReadAll"));
    message << QStringList{QStringLiteral("org.freedesktop.appearance"),
                           QStringLiteral("org.gnome.desktop.interface")};
    const QDBusMessage reply = bus.call(message, QDBus::Block, timeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()
            || reply.signature() != QStringLiteral("a{sa{sv}}"))
        return settings;
    reply.arguments().constFirst().value<QDBusArgument>() >> settings;
    return settings;
}

} // namespace

QVariant PortalSettings::read(const QString &nameSpace, const QString &key) {
    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return {};

    QDBusMessage message = request(QStringLiteral("Read"));
    message << nameSpace << key;
    const QDBusReply<QDBusVariant> reply(bus.call(message, QDBus::Block, timeoutMs));
    if (!reply.isValid())
        return {};

    return reply.value().variant();
}

QVariant PortalSettings::atStartup(const QString &nameSpace, const QString &key) {
    static const QMap<QString, QVariantMap> settings = readAll();
    return settings.value(nameSpace).value(key);
}
