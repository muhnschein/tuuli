// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
//
// Modelled on sailfish-browser apps/history/declarativetabmodel.{h,cpp}
// (Copyright (c) 2013 Jolla Ltd., (c) 2021 Open Mobile Platform LLC, MPL-2.0).
// Differences: no web container coupling, no thumbnails, private tabs are a per-tab
// flag, and the model reports navigations through signals instead of writing history.
#pragma once

#include "Tab.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>

namespace Tuuli {

class TabPersistence;

class TabModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int activeTabIndex READ activeTabIndex NOTIFY activeTabChanged)
    Q_PROPERTY(int activeTabId READ activeTabId NOTIFY activeTabChanged)
    Q_PROPERTY(bool activeIsPrivate READ activeIsPrivate NOTIFY activeTabChanged)
    Q_PROPERTY(QString activeUrl READ activeUrl NOTIFY activeTabDataChanged)
    Q_PROPERTY(QString activeTitle READ activeTitle NOTIFY activeTabDataChanged)
    Q_PROPERTY(QString activeFavicon READ activeFavicon NOTIFY activeTabDataChanged)

public:
    enum Role
    {
        TabIdRole = Qt::UserRole + 1,
        UrlRole,
        TitleRole,
        FaviconRole,
        PrivateRole,
        ActiveRole
    };

    // A null persistence keeps the model in memory only (used by tests).
    explicit TabModel(TabPersistence *persistence, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    int activeTabIndex() const;
    int activeTabId() const;
    bool activeIsPrivate() const;
    QString activeUrl() const;
    QString activeTitle() const;
    QString activeFavicon() const;
    const QList<Tab> &tabs() const;

    // Returns the new tab id, or 0 when the url is handed to another app (tel:, sms:, ...).
    Q_INVOKABLE int newTab(const QString &url, bool isPrivate = false);
    Q_INVOKABLE void activateTab(int index);
    Q_INVOKABLE bool activateTabById(int tabId);
    Q_INVOKABLE void closeTab(int index);
    Q_INVOKABLE void closeActiveTab();
    Q_INVOKABLE void closeAllTabs();
    Q_INVOKABLE int indexOf(int tabId) const;

    // Called by the view as the engine reports page state.
    Q_INVOKABLE void updateUrl(int tabId, const QString &url);
    Q_INVOKABLE void updateTitle(int tabId, const QString &title);
    Q_INVOKABLE void updateFavicon(int tabId, const QString &favicon);

    static bool isExternalUrl(const QString &url);

signals:
    void countChanged();
    void activeTabChanged();
    void activeTabDataChanged();
    void tabAdded(int tabId);
    void tabClosed(int tabId);
    // Emitted for non-private tabs only; wired to the history model.
    void visited(const QString &url);
    void titleUpdated(const QString &url, const QString &title);
    void faviconUpdated(const QString &url, const QString &favicon);

private:
    void load();
    void setActiveTab(int tabId);
    void notifyRow(int index, Role role);
    void persist(const Tab &tab);

    TabPersistence *m_persistence;
    QList<Tab> m_tabs;
    QList<int> m_awaitingFirstUrl;
    int m_activeTabId = 0;
    int m_nextTabId = 1;
};

} // namespace Tuuli
