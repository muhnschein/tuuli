/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_BROWSERCONTEXT_H
#define TUULI_BROWSERCONTEXT_H

/*
 * The `Browser` QML singleton: owns the engine, the models and the stores
 * and wires them together (session persistence, history, permissions,
 * downloads, cosmetic filtering, proxy).  One per process.
 */

#include "blocking/cosmeticfilter.h"
#include "engine/engine.h"
#include "model/bookmarkmodel.h"
#include "model/downloadmanager.h"
#include "model/historymodel.h"
#include "model/permissionstore.h"
#include "model/sessionstore.h"
#include "model/tabmodel.h"
#include "perf/perflog.h"
#include "platform/clipboardbridge.h"
#include "platform/connmanproxy.h"
#include "platform/transferengine.h"
#include "prefs/preferences.h"

#include <QObject>
#include <QSet>
#include <QUrl>
#include <QVariantList>

namespace Tuuli {

class TuuliImageProvider;
class TuuliWebView;

class BrowserContext : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Tuuli::TabModel* tabs READ tabs CONSTANT)
    Q_PROPERTY(Tuuli::HistoryModel* history READ history CONSTANT)
    Q_PROPERTY(Tuuli::BookmarkModel* bookmarks READ bookmarks CONSTANT)
    Q_PROPERTY(Tuuli::DownloadManager* downloads READ downloads CONSTANT)
    Q_PROPERTY(Tuuli::PermissionStore* permissions READ permissions CONSTANT)
    Q_PROPERTY(Tuuli::Preferences* prefs READ prefs CONSTANT)
    Q_PROPERTY(Tuuli::ClipboardBridge* clipboard READ clipboard CONSTANT)
    Q_PROPERTY(QString engineName READ engineName CONSTANT)
    Q_PROPERTY(QString engineVersion READ engineVersion CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(bool restoredAfterCrash READ restoredAfterCrash NOTIFY restoredAfterCrashChanged)
    Q_PROPERTY(QString engineError READ engineError NOTIFY engineErrorChanged)
    Q_PROPERTY(QVariantList searchEngines READ searchEngines CONSTANT)
    Q_PROPERTY(QString dataDirectory READ dataDirectory CONSTANT)
    Q_PROPERTY(int cosmeticRuleCount READ cosmeticRuleCount NOTIFY cosmeticRulesChanged)
    Q_PROPERTY(bool proxyActive READ proxyActive NOTIFY proxyChanged)

public:
    struct Paths {
        QString dataDir;
        QString cacheDir;
        QString configDir;
    };

    /* Creates the singleton.  Takes ownership of `engine`. */
    static BrowserContext* create(Engine* engine, const Paths& paths, QObject* parent = nullptr);
    /* Creates the singleton with the configured engine and the XDG paths
     * the sailjail profile permits (spec 9.1), and starts it, unless one
     * exists already. */
    static BrowserContext* ensureCreated();
    static BrowserContext* instance();
    ~BrowserContext();

    Engine* engine() const { return m_engine; }
    TabModel* tabs() const { return m_tabs; }
    HistoryModel* history() const { return m_history; }
    BookmarkModel* bookmarks() const { return m_bookmarks; }
    DownloadManager* downloads() const { return m_downloads; }
    PermissionStore* permissions() const { return m_permissions; }
    Preferences* prefs() const { return m_prefs; }
    ClipboardBridge* clipboard() const { return m_clipboard; }
    SessionStore* session() const { return m_session; }
    PerfLog* perfLog() const { return m_perfLog; }
    TuuliImageProvider* imageProvider() const { return m_imageProvider; }
    const CosmeticFilter& cosmeticFilter() const { return m_filter; }

    QString engineName() const { return m_engine->name(); }
    QString engineVersion() const { return m_engine->versionString(); }
    QString version() const;
    bool restoredAfterCrash() const { return m_restoredAfterCrash; }
    QString engineError() const { return m_engineError; }
    QVariantList searchEngines() const;
    QString dataDirectory() const { return m_paths.dataDir; }
    int cosmeticRuleCount() const;
    bool proxyActive() const { return !m_connman->current().isDirect(); }

    /* Startup: restore the previous session (or open the start page) and
     * handle command-line URLs. */
    void start(const QStringList& arguments);
    void restoreSession();
    int loadCosmeticRules(const QString& directory);

    Q_INVOKABLE QUrl resolveInput(const QString& input) const;
    Q_INVOKABLE void openUrl(const QUrl& url, bool isPrivate = false, bool inNewTab = true);
    Q_INVOKABLE void openInput(const QString& input, bool isPrivate = false, bool inNewTab = false);
    Q_INVOKABLE void saveSessionNow();
    Q_INVOKABLE QString searchEngineName(const QString& id) const;
    Q_INVOKABLE void clearBrowsingData(bool history, bool cookies, bool cache, bool storage, bool permissions);
    Q_INVOKABLE void rememberPermission(const QString& origin, int kind, bool allow, bool isPrivate);
    Q_INVOKABLE void reloadCosmeticRules();
    /* In-app notice for the chrome (system notifications are M4). */
    Q_INVOKABLE void notify(const QString& text) { emit notificationRequested(QString(), text); }
    /* Share sheet; handled by the ApplicationWindow with Sailfish.Share. */
    Q_INVOKABLE void share(const QUrl& url, const QString& title) { emit shareRequested(url, title); }

    void registerWebView(TuuliWebView* view);
    void unregisterWebView(TuuliWebView* view);

public slots:
    /* window.open: the engine created the webview; give it a tab. */
    void adoptAuxiliaryWebView(Tuuli::WebViewHandle* handle);

signals:
    void restoredAfterCrashChanged();
    void engineErrorChanged();
    void cosmeticRulesChanged();
    void proxyChanged();
    /* For the Silica chrome (spec 8.3: every prompt is a dialog, denied by
     * default). */
    void permissionPrompt(Tuuli::PermissionRequest* request, bool isPrivate);
    void dialogPrompt(Tuuli::SimpleDialogRequest* request, bool isPrivate);
    void notificationRequested(const QString& title, const QString& body);
    void downloadStarted(const QString& fileName);
    void engineCrashed(const QString& reason);
    void shareRequested(const QUrl& url, const QString& title);

private:
    BrowserContext(Engine* engine, const Paths& paths, QObject* parent);
    void configureEngine();
    void connectTab(Tab* tab);
    void applyCosmeticFilter(Tab* tab, const QUrl& url);
    void onApplicationStateChanged(Qt::ApplicationState state);
    void onAboutToQuit();
    void pushEnginePrefs();

    static BrowserContext* s_instance;

    Paths m_paths;
    Engine* m_engine;
    Preferences* m_prefs;
    TabModel* m_tabs;
    HistoryModel* m_history;
    BookmarkModel* m_bookmarks;
    PermissionStore* m_permissions;
    SessionStore* m_session;
    TransferEngine* m_transfers;
    DownloadManager* m_downloads;
    ClipboardBridge* m_clipboard;
    ConnmanProxy* m_connman;
    TuuliImageProvider* m_imageProvider;
    PerfLog* m_perfLog;
    CosmeticFilter m_filter;
    QSet<TuuliWebView*> m_views;
    bool m_restoredAfterCrash = false;
    QString m_engineError;
    bool m_started = false;
};

} // namespace Tuuli

#endif
