/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "connmanproxy.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>

namespace Tuuli {

static const char* kConnmanService = "net.connman";
static const char* kConnmanManagerIface = "net.connman.Manager";

ConnmanProxy::ConnmanProxy(QObject* parent)
    : QObject(parent)
{
}

QString ConnmanProxy::stripScheme(const QString& server)
{
    QString s = server.trimmed();
    const int idx = s.indexOf(QLatin1String("://"));
    if (idx >= 0)
        s = s.mid(idx + 3);
    while (s.endsWith(QLatin1Char('/')))
        s.chop(1);
    return s;
}

ProxyConfig ConnmanProxy::fromProxyProperties(const QVariantMap& proxy)
{
    ProxyConfig cfg;
    const QString method = proxy.value(QStringLiteral("Method")).toString().toLower();
    if (method == QLatin1String("manual")) {
        const QStringList servers = proxy.value(QStringLiteral("Servers")).toStringList();
        for (const QString& raw : servers) {
            const QString server = stripScheme(raw);
            if (server.isEmpty())
                continue;
            const QString lower = raw.trimmed().toLower();
            if (lower.startsWith(QLatin1String("https://"))) {
                if (cfg.https.isEmpty())
                    cfg.https = server;
            } else {
                if (cfg.http.isEmpty())
                    cfg.http = server;
            }
        }
        if (cfg.https.isEmpty())
            cfg.https = cfg.http;
        if (cfg.http.isEmpty())
            cfg.http = cfg.https;
        cfg.noProxy = proxy.value(QStringLiteral("Excludes")).toStringList();
    } else if (method == QLatin1String("auto")) {
        cfg.pacUrl = QUrl(proxy.value(QStringLiteral("URL")).toString());
    }
    return cfg;
}

void ConnmanProxy::start()
{
    if (m_started)
        return;
    m_started = true;
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected())
        return;
    m_available = bus.connect(QString::fromLatin1(kConnmanService), QStringLiteral("/"),
                              QString::fromLatin1(kConnmanManagerIface), QStringLiteral("ServicesChanged"),
                              this, SLOT(onServicesChanged()));
    refresh();
}

void ConnmanProxy::onServicesChanged()
{
    refresh();
}

void ConnmanProxy::refresh()
{
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(QString::fromLatin1(kConnmanService), QStringLiteral("/"),
                                                       QString::fromLatin1(kConnmanManagerIface),
                                                       QStringLiteral("GetServices"));
    const QDBusMessage reply = bus.call(call, QDBus::Block, 2000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return;
    m_available = true;

    const QDBusArgument services = reply.arguments().first().value<QDBusArgument>();
    QVariantMap chosen;
    services.beginArray();
    while (!services.atEnd()) {
        services.beginStructure();
        QDBusObjectPath path;
        QVariantMap props;
        services >> path >> props;
        services.endStructure();
        const QString state = props.value(QStringLiteral("State")).toString();
        if (chosen.isEmpty() && (state == QLatin1String("online") || state == QLatin1String("ready")))
            chosen = props;
    }
    services.endArray();
    applyServiceProperties(chosen);
}

void ConnmanProxy::applyServiceProperties(const QVariantMap& props)
{
    QVariantMap proxy;
    const QVariant v = props.value(QStringLiteral("Proxy"));
    if (v.canConvert<QDBusArgument>())
        v.value<QDBusArgument>() >> proxy;
    else
        proxy = v.toMap();
    const ProxyConfig cfg = fromProxyProperties(proxy);
    if (cfg != m_current) {
        m_current = cfg;
        emit proxyChanged(cfg);
    }
}

} // namespace Tuuli
