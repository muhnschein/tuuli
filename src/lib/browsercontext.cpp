/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "browsercontext.h"
#include "engine/enginefactory.h"
#include "prefs/searchengines.h"
#include "prefs/useragent.h"
#include "tuuli_global.h"
#include "view/imageprovider.h"
#include "view/tuuliwebview.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QVariantMap>

namespace Tuuli {

BrowserContext* BrowserContext::s_instance = nullptr;

static const char* kCosmeticStylesheetId = "tuuli-cosmetic";

BrowserContext* BrowserContext::create(Engine* engine, const Paths& paths, QObject* parent)
{
    if (s_instance)
        return s_instance;
    s_instance = new BrowserContext(engine, paths, parent);
    return s_instance;
}

BrowserContext* BrowserContext::ensureCreated()
{
    if (s_instance)
        return s_instance;
    Paths paths;
    paths.dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    paths.cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    paths.configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    BrowserContext* ctx = create(createDefaultEngine(), paths, nullptr);
    ctx->start(QCoreApplication::arguments());
    return ctx;
}

BrowserContext* BrowserContext::instance()
{
    return s_instance;
}

BrowserContext::BrowserContext(Engine* engine, const Paths& paths, QObject* parent)
    : QObject(parent)
    , m_paths(paths)
    , m_engine(engine)
{
    qRegisterMetaType<Tuuli::PermissionRequest*>("Tuuli::PermissionRequest*");
    qRegisterMetaType<Tuuli::SimpleDialogRequest*>("Tuuli::SimpleDialogRequest*");
    qRegisterMetaType<Tuuli::DownloadRequest*>("Tuuli::DownloadRequest*");
    qRegisterMetaType<Tuuli::WebViewHandle*>("Tuuli::WebViewHandle*");
    qRegisterMetaType<Tuuli::ContextMenuInfo>("Tuuli::ContextMenuInfo");
    qRegisterMetaType<Tuuli::MediaSessionInfo>("Tuuli::MediaSessionInfo");
    qRegisterMetaType<Tuuli::ProxyConfig>("Tuuli::ProxyConfig");

    QDir().mkpath(m_paths.dataDir);
    QDir().mkpath(m_paths.cacheDir);
    QDir().mkpath(m_paths.configDir);

    m_engine->setParent(this);
    m_prefs = new Preferences(m_paths.configDir + QStringLiteral("/tuuli.conf"), this);
    m_tabs = new TabModel(m_engine, this);
    m_history = new HistoryModel(m_paths.dataDir + QStringLiteral("/history.sqlite"), this);
    m_bookmarks = new BookmarkModel(m_paths.dataDir + QStringLiteral("/bookmarks.sqlite"), this);
    m_permissions = new PermissionStore(m_paths.dataDir + QStringLiteral("/permissions.json"), this);
    m_session = new SessionStore(m_paths.dataDir + QStringLiteral("/session.json"), this);
    m_transfers = new TransferEngine(this);
    m_downloads = new DownloadManager(m_transfers, this);
    m_clipboard = new ClipboardBridge(this);
    m_connman = new ConnmanProxy(this);
    m_imageProvider = new TuuliImageProvider(); // owned by the QML engine once added
    m_perfLog = new PerfLog(m_paths.cacheDir + QStringLiteral("/perf.log"), this);
    m_perfLog->setEnabled(m_prefs->perfLogging());

    m_tabs->setMaxLiveWebViews(m_prefs->maxLiveWebViews());
    m_downloads->setDirectory(m_prefs->downloadDirectory());

    configureEngine();

    connect(m_tabs, &TabModel::tabAdded, this, &BrowserContext::connectTab);
    connect(m_tabs, &TabModel::tabClosed, this, [this](int id) { m_imageProvider->removeTab(id); });
    connect(m_tabs, &TabModel::sessionChanged, this, [this]() {
        if (m_started)
            m_session->scheduleSave(m_tabs->snapshot());
    });
    connect(m_tabs, &TabModel::countChanged, this, [this]() {
        if (m_tabs->privateCount() == 0)
            m_downloads->clearPrivate();
    });

    connect(m_prefs, &Preferences::privacyChanged, this, &BrowserContext::pushEnginePrefs);
    connect(m_prefs, &Preferences::engineChanged, this, &BrowserContext::pushEnginePrefs);
    connect(m_prefs, &Preferences::downloadDirectoryChanged, this, [this]() {
        m_downloads->setDirectory(m_prefs->downloadDirectory());
    });
    connect(m_prefs, &Preferences::developerChanged, this, [this]() {
        m_tabs->setMaxLiveWebViews(m_prefs->maxLiveWebViews());
        m_perfLog->setEnabled(m_prefs->perfLogging());
    });

    connect(m_connman, &ConnmanProxy::proxyChanged, this, [this](const ProxyConfig& proxy) {
        m_engine->setProxy(proxy);
        emit proxyChanged();
    });
    m_connman->start();

    connect(m_engine, &Engine::crashed, this, [this](const QString& reason, const QString& backtrace) {
        Q_UNUSED(backtrace);
        m_engineError = reason;
        m_session->flush();
        emit engineErrorChanged();
        emit engineCrashed(reason);
    });
    if (m_engine->metaObject()->indexOfSignal("auxiliaryWebViewCreated(Tuuli::WebViewHandle*)") >= 0) {
        connect(m_engine, SIGNAL(auxiliaryWebViewCreated(Tuuli::WebViewHandle*)),
                this, SLOT(adoptAuxiliaryWebView(Tuuli::WebViewHandle*)));
    }

    if (QCoreApplication* app = QCoreApplication::instance()) {
        connect(app, &QCoreApplication::aboutToQuit, this, &BrowserContext::onAboutToQuit);
        if (QGuiApplication* gui = qobject_cast<QGuiApplication*>(app))
            connect(gui, &QGuiApplication::applicationStateChanged, this, &BrowserContext::onApplicationStateChanged);
    }
}

BrowserContext::~BrowserContext()
{
    if (s_instance == this)
        s_instance = nullptr;
}

QString BrowserContext::version() const
{
    return QStringLiteral(TUULI_VERSION_STRING);
}

void BrowserContext::configureEngine()
{
    EngineConfig cfg;
    cfg.userAgent = m_prefs->userAgentOverride();
    if (cfg.userAgent.isEmpty())
        cfg.userAgent = UserAgent::mobile(m_engine->versionString(), version());
    cfg.mobilePlatform = true;
    // Spec 8.1: system CA bundle, never our own roots.
    const QStringList caCandidates = {
        QStringLiteral("/etc/pki/tls/certs/ca-bundle.crt"),
        QStringLiteral("/etc/ssl/certs/ca-certificates.crt"),
    };
    for (const QString& c : caCandidates)
        if (QFile::exists(c)) { cfg.certificatePath = c; break; }
    cfg.dataDir = m_paths.dataDir + QStringLiteral("/engine");
    cfg.cacheDir = m_paths.cacheDir + QStringLiteral("/engine");
    cfg.proxy = m_connman->current();
    cfg.prefs = m_prefs->enginePrefs();
    cfg.hardwareVideoDecode = true;
    m_engine->configure(cfg);
}

void BrowserContext::pushEnginePrefs()
{
    for (const QString& line : m_prefs->enginePrefs()) {
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq > 0)
            m_engine->setPref(line.left(eq), line.mid(eq + 1));
    }
}

void BrowserContext::connectTab(Tab* tab)
{
    connect(tab, &Tab::urlChangedSignal, this, [this, tab](const QUrl& url) {
        m_history->addVisit(url, tab->title(), tab->isPrivate());
        applyCosmeticFilter(tab, url);
        m_perfLog->navigationStarted(tab->tabId(), url);
    });
    connect(tab, &Tab::loadFinished, this, [this, tab]() { m_perfLog->loadFinished(tab->tabId()); });
    connect(tab, &Tab::frameReadySignal, this, [this, tab]() {
        m_perfLog->markFirstPaint(!m_restoredAfterCrash);
        m_perfLog->frameReady(tab->tabId(), m_tabs->count());
    });
    connect(tab, &Tab::titleChangedSignal, this, [this, tab](const QString& title) {
        m_history->updateTitle(tab->url(), title, tab->isPrivate());
    });
    connect(tab, &Tab::faviconChanged, this, [this, tab]() { m_imageProvider->setFavicon(tab->tabId(), tab->favicon()); });
    connect(tab, &Tab::thumbnailChanged, this, [this, tab]() { m_imageProvider->setThumbnail(tab->tabId(), tab->thumbnail()); });
    connect(tab, &Tab::permissionRequest, this, [this, tab](PermissionRequest* req) {
        if (!m_permissions->answerFromStore(req))
            emit permissionPrompt(req, tab->isPrivate());
    });
    connect(tab, &Tab::dialogRequest, this, [this, tab](SimpleDialogRequest* req) {
        emit dialogPrompt(req, tab->isPrivate());
    });
    connect(tab, &Tab::downloadRequest, this, [this, tab](DownloadRequest* req) {
        m_downloads->handleRequest(req, tab->isPrivate());
        emit downloadStarted(req->suggestedName());
    });
    connect(tab, &Tab::notification, this, [this](const QString& title, const QString& body, const QUrl&) {
        // Nemo notifications are M4 (spec 8.3); surface in-app until then.
        emit notificationRequested(title, body);
    });
    connect(tab, &Tab::hasWebViewChanged, this, [this, tab]() {
        if (tab->hasWebView())
            applyCosmeticFilter(tab, tab->url());
    });
}

void BrowserContext::applyCosmeticFilter(Tab* tab, const QUrl& url)
{
    if (!tab->hasWebView())
        return;
    if (!m_prefs->cosmeticFiltering() || m_filter.isEmpty()) {
        tab->removeUserStylesheet(QLatin1String(kCosmeticStylesheetId));
        return;
    }
    const QString css = m_filter.stylesheetFor(url.host().toLower());
    if (css.isEmpty())
        tab->removeUserStylesheet(QLatin1String(kCosmeticStylesheetId));
    else
        tab->setUserStylesheet(QLatin1String(kCosmeticStylesheetId), css);
}

int BrowserContext::loadCosmeticRules(const QString& directory)
{
    m_filter.clear();
    QDir dir(directory);
    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.txt"), QDir::Files, QDir::Name);
    for (const QString& f : files)
        m_filter.loadFile(dir.absoluteFilePath(f));
    emit cosmeticRulesChanged();
    return m_filter.stats().genericRules + m_filter.stats().domainRules;
}

void BrowserContext::reloadCosmeticRules()
{
    loadCosmeticRules(m_paths.dataDir + QStringLiteral("/filters"));
    for (int i = 0; i < m_tabs->count(); ++i)
        applyCosmeticFilter(m_tabs->tabAt(i), m_tabs->tabAt(i)->url());
}

int BrowserContext::cosmeticRuleCount() const
{
    return m_filter.stats().genericRules + m_filter.stats().domainRules;
}

void BrowserContext::start(const QStringList& arguments)
{
    if (m_started)
        return;
    loadCosmeticRules(m_paths.dataDir + QStringLiteral("/filters"));
    restoreSession();
    m_started = true;

    for (int i = 1; i < arguments.size(); ++i) {
        const QString a = arguments.at(i);
        if (a.startsWith(QLatin1Char('-')))
            continue;
        const QUrl url = resolveInput(a);
        if (!url.isEmpty())
            openUrl(url, false, true);
    }
    // A fresh session file marks "running"; a clean exit rewrites it.
    m_session->saveNow(m_tabs->snapshot());
}

void BrowserContext::restoreSession()
{
    bool ok = false;
    const bool existed = m_session->exists();
    const Session s = m_session->load(&ok);
    m_restoredAfterCrash = existed && ok && !s.cleanExit && !s.tabs.isEmpty();
    if (ok && m_prefs->restoreSession() && !s.tabs.isEmpty())
        m_tabs->restore(s);
    emit restoredAfterCrashChanged();
}

void BrowserContext::saveSessionNow()
{
    m_session->saveNow(m_tabs->snapshot());
}

void BrowserContext::onApplicationStateChanged(Qt::ApplicationState state)
{
    // Spec 8.4: every backgrounding flushes.
    if (state != Qt::ApplicationActive)
        m_session->saveNow(m_tabs->snapshot());
}

void BrowserContext::onAboutToQuit()
{
    Session s = m_tabs->snapshot();
    s.cleanExit = true;
    m_session->saveNow(s);
    m_prefs->sync();
}

QUrl BrowserContext::resolveInput(const QString& input) const
{
    return SearchEngines::resolve(input, m_prefs->searchEngine());
}

void BrowserContext::openUrl(const QUrl& url, bool isPrivate, bool inNewTab)
{
    if (url.isEmpty())
        return;
    Tab* current = m_tabs->currentTab();
    // Never mix a private and a non-private document in one webview (7.3).
    if (!inNewTab && current && current->isPrivate() == isPrivate) {
        current->load(url);
        return;
    }
    m_tabs->newTab(url, isPrivate, true);
}

void BrowserContext::openInput(const QString& input, bool isPrivate, bool inNewTab)
{
    openUrl(resolveInput(input), isPrivate, inNewTab);
}

QVariantList BrowserContext::searchEngines() const
{
    QVariantList out;
    for (const SearchEngine& e : SearchEngines::builtin()) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), e.id);
        m.insert(QStringLiteral("name"), e.name);
        m.insert(QStringLiteral("homeUrl"), e.homeUrl);
        out.append(m);
    }
    return out;
}

QString BrowserContext::searchEngineName(const QString& id) const
{
    const SearchEngine* e = SearchEngines::byId(id);
    return e ? e->name : QString();
}

void BrowserContext::clearBrowsingData(bool history, bool cookies, bool cache, bool storage, bool permissions)
{
    if (history)
        m_history->clear();
    unsigned kinds = 0;
    if (cookies) kinds |= static_cast<unsigned>(SiteDataKind::Cookies);
    if (cache) kinds |= static_cast<unsigned>(SiteDataKind::HttpCache);
    if (storage) kinds |= static_cast<unsigned>(SiteDataKind::LocalStorage) | static_cast<unsigned>(SiteDataKind::SessionStorage);
    if (kinds)
        m_engine->clearSiteData(QString(), kinds);
    if (permissions)
        m_permissions->clearAll();
}

void BrowserContext::rememberPermission(const QString& origin, int kind, bool allow, bool isPrivate)
{
    if (isPrivate)
        return; // spec 7.3: private contexts never persist
    m_permissions->setDecision(origin, kind, allow ? PermissionStore::Allow : PermissionStore::Deny);
}

void BrowserContext::adoptAuxiliaryWebView(WebViewHandle* handle)
{
    if (!handle)
        return;
    Tab* tab = m_tabs->newTab(QUrl(), handle->isPrivate(), true);
    tab->attachWebView(handle);
    handle->setClient(tab);
}

void BrowserContext::registerWebView(TuuliWebView* view) { m_views.insert(view); }
void BrowserContext::unregisterWebView(TuuliWebView* view) { m_views.remove(view); }

} // namespace Tuuli
