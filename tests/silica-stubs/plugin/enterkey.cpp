// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
#include "enterkey.h"

EnterKeyAttached::EnterKeyAttached(QObject *parent)
    : QObject(parent)
{
}

bool EnterKeyAttached::enabled() const
{
    return m_enabled;
}

void EnterKeyAttached::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    emit enabledChanged();
}

QUrl EnterKeyAttached::iconSource() const
{
    return m_iconSource;
}

void EnterKeyAttached::setIconSource(const QUrl &source)
{
    if (m_iconSource == source) {
        return;
    }
    m_iconSource = source;
    emit iconSourceChanged();
}

QString EnterKeyAttached::text() const
{
    return m_text;
}

void EnterKeyAttached::setText(const QString &text)
{
    if (m_text == text) {
        return;
    }
    m_text = text;
    emit textChanged();
}

EnterKeyAttached *EnterKey::qmlAttachedProperties(QObject *object)
{
    // Named so tests can reach the attached object without type-name lookups, which
    // would be ambiguous with QtQuick's own EnterKey attached property.
    auto *attached = new EnterKeyAttached(object);
    attached->setObjectName(QStringLiteral("EnterKeyAttached"));
    return attached;
}
