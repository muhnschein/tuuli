/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_CONNMANPROXY_H
#define TUULI_CONNMANPROXY_H

/*
 * Reads the proxy configuration of the active connman service (spec 8.1)
 * over the system bus and re-reads it when services change.  The pure
 * conversion from connman's "Proxy" property dict is separated so it can
 * be unit-tested without D-Bus.
 */

#include "engine/engine.h"

#include <QObject>
#include <QVariantMap>

namespace Tuuli {

class ConnmanProxy : public QObject
{
    Q_OBJECT
public:
    explicit ConnmanProxy(QObject* parent = nullptr);

    ProxyConfig current() const { return m_current; }
    bool available() const { return m_available; }

    /* Connect to connman and read the active service.  No-op on hosts
     * without connman; current() stays direct. */
    void start();
    void refresh();

    static ProxyConfig fromProxyProperties(const QVariantMap& proxy);
    static QString stripScheme(const QString& server);

signals:
    void proxyChanged(const Tuuli::ProxyConfig& proxy);

private:
    void onServicesChanged();
    void applyServiceProperties(const QVariantMap& props);

    ProxyConfig m_current;
    bool m_available = false;
    bool m_started = false;
};

} // namespace Tuuli

#endif
