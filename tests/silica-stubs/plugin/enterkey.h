// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
//
// Stand-in for Silica's EnterKey attached property. Attached properties need C++,
// which is the only reason the stub module carries a plugin.
#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml>

class EnterKeyAttached : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QUrl iconSource READ iconSource WRITE setIconSource NOTIFY iconSourceChanged)
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)

public:
    explicit EnterKeyAttached(QObject *parent = nullptr);

    bool enabled() const;
    void setEnabled(bool enabled);
    QUrl iconSource() const;
    void setIconSource(const QUrl &source);
    QString text() const;
    void setText(const QString &text);

signals:
    void clicked();
    void enabledChanged();
    void iconSourceChanged();
    void textChanged();

private:
    bool m_enabled = true;
    QUrl m_iconSource;
    QString m_text;
};

class EnterKey : public QObject
{
    Q_OBJECT

public:
    static EnterKeyAttached *qmlAttachedProperties(QObject *object);
};

QML_DECLARE_TYPEINFO(EnterKey, QML_HAS_ATTACHED_PROPERTIES)
