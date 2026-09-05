// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Sailfish platform bridges over Qt: Nemo Transfer Engine and connman on
//! D-Bus (spec 7.1, 8.1), the system clipboard (spec 8.3) and the
//! `image://tuuli/` provider for favicons and thumbnails.  Every call is
//! best-effort: on a host without the services it degrades to a no-op.

use std::cell::RefCell;
use std::collections::HashMap;
use std::ffi::c_void;
use std::path::Path;

use cpp::cpp;
use qttypes::{QString, QStringList};
use tuuli_core::downloads::{TransferEngine, TransferStatus};
use tuuli_core::engine::RgbaImage;
use tuuli_core::proxy::ProxyConfig;

cpp! {{
    #include <QtCore/QCoreApplication>
    #include <QtCore/QStringList>
    #include <QtCore/QVariant>
    #include <QtDBus/QDBusArgument>
    #include <QtDBus/QDBusConnection>
    #include <QtDBus/QDBusConnectionInterface>
    #include <QtDBus/QDBusInterface>
    #include <QtDBus/QDBusMessage>
    #include <QtDBus/QDBusObjectPath>
    #include <QtDBus/QDBusReply>
    #include <QtGui/QClipboard>
    #include <QtGui/QGuiApplication>
    #include <QtGui/QImage>
    #include <QtQml/QQmlEngine>
    #include <QtQuick/QQuickImageProvider>

    static const char *kTransferService = "org.nemo.transferengine";
    static const char *kTransferPath = "/org/nemo/transferengine";
    static const char *kTransferIface = "org.nemo.transferengine";
}}

// ---- Transfer Engine ------------------------------------------------------------

pub struct DBusTransferEngine;

impl DBusTransferEngine {
    /// `None` when there is no session bus at all (host tests).
    pub fn new() -> Option<Self> {
        let ok = cpp!(unsafe [] -> bool as "bool" {
            return QDBusConnection::sessionBus().isConnected();
        });
        if ok {
            Some(Self)
        } else {
            None
        }
    }

    pub fn available(&self) -> bool {
        cpp!(unsafe [] -> bool as "bool" {
            QDBusConnection bus = QDBusConnection::sessionBus();
            if (!bus.isConnected() || !bus.interface()) return false;
            QDBusReply<bool> reg = bus.interface()->isServiceRegistered(QString::fromLatin1(kTransferService));
            if (reg.isValid() && reg.value()) return true;
            QDBusReply<QStringList> act = bus.interface()->call(QStringLiteral("ListActivatableNames"));
            return act.isValid() && act.value().contains(QString::fromLatin1(kTransferService));
        })
    }
}

impl TransferEngine for DBusTransferEngine {
    fn create_download(
        &self,
        display_name: &str,
        path: &Path,
        mime: &str,
        expected_size: i64,
    ) -> Option<i32> {
        let name = QString::from(display_name);
        let file = QString::from(path.to_string_lossy().to_string());
        let mime = QString::from(mime);
        let id = cpp!(unsafe [name as "QString", file as "QString", mime as "QString", expected_size as "qlonglong"] -> i32 as "int" {
            QDBusInterface iface(QString::fromLatin1(kTransferService), QString::fromLatin1(kTransferPath), QString::fromLatin1(kTransferIface), QDBusConnection::sessionBus());
            if (!iface.isValid()) return -1;
            // Cancel/restart callbacks are served by the browser's own D-Bus
            // object (org.tuuli.browser.Downloads).
            QStringList callback;
            callback << QStringLiteral("org.tuuli.browser") << QStringLiteral("/org/tuuli/browser/downloads") << QStringLiteral("org.tuuli.browser.Downloads");
            QDBusReply<int> reply = iface.call(QStringLiteral("createDownload"), name,
                QStringLiteral("icon-launcher-tuuli-browser"), QStringLiteral("icon-s-cloud-download"),
                file, mime, expected_size, callback, QStringLiteral("cancelTransfer"), QStringLiteral("restartTransfer"));
            return reply.isValid() ? reply.value() : -1;
        });
        if id >= 0 {
            Some(id)
        } else {
            None
        }
    }

    fn start(&self, transfer_id: i32) {
        cpp!(unsafe [transfer_id as "int"] {
            QDBusInterface iface(QString::fromLatin1(kTransferService), QString::fromLatin1(kTransferPath), QString::fromLatin1(kTransferIface), QDBusConnection::sessionBus());
            if (iface.isValid()) iface.asyncCall(QStringLiteral("startTransfer"), transfer_id);
        });
    }

    fn update_progress(&self, transfer_id: i32, progress: f64) {
        cpp!(unsafe [transfer_id as "int", progress as "double"] {
            QDBusInterface iface(QString::fromLatin1(kTransferService), QString::fromLatin1(kTransferPath), QString::fromLatin1(kTransferIface), QDBusConnection::sessionBus());
            if (iface.isValid()) iface.asyncCall(QStringLiteral("updateTransferProgress"), transfer_id, progress);
        });
    }

    fn finish(&self, transfer_id: i32, status: TransferStatus, reason: &str) {
        let status = status as i32;
        let reason = QString::from(reason);
        cpp!(unsafe [transfer_id as "int", status as "int", reason as "QString"] {
            QDBusInterface iface(QString::fromLatin1(kTransferService), QString::fromLatin1(kTransferPath), QString::fromLatin1(kTransferIface), QDBusConnection::sessionBus());
            if (iface.isValid()) iface.asyncCall(QStringLiteral("finishTransfer"), transfer_id, status, reason);
        });
    }
}

// ---- connman ------------------------------------------------------------------------

/// Reads the active connman service's `Proxy` property (spec 8.1).
/// `None` when connman is unreachable.
pub fn connman_read_proxy() -> Option<ProxyConfig> {
    let mut method = QString::default();
    let mut servers = QStringList::default();
    let mut excludes = QStringList::default();
    let mut url = QString::default();
    let ok = cpp!(unsafe [mut method as "QString", mut servers as "QStringList", mut excludes as "QStringList", mut url as "QString"] -> bool as "bool" {
        QDBusConnection bus = QDBusConnection::systemBus();
        if (!bus.isConnected()) return false;
        QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("net.connman"), QStringLiteral("/"),
                                                           QStringLiteral("net.connman.Manager"), QStringLiteral("GetServices"));
        QDBusMessage reply = bus.call(call, QDBus::Block, 2000);
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) return false;
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
        QVariantMap proxy;
        const QVariant v = chosen.value(QStringLiteral("Proxy"));
        if (v.canConvert<QDBusArgument>()) v.value<QDBusArgument>() >> proxy; else proxy = v.toMap();
        method = proxy.value(QStringLiteral("Method")).toString();
        servers = proxy.value(QStringLiteral("Servers")).toStringList();
        excludes = proxy.value(QStringLiteral("Excludes")).toStringList();
        url = proxy.value(QStringLiteral("URL")).toString();
        return true;
    });
    if !ok {
        return None;
    }
    let servers: Vec<String> = servers.into_iter().map(|s| s.to_string()).collect();
    let excludes: Vec<String> = excludes.into_iter().map(|s| s.to_string()).collect();
    Some(ProxyConfig::from_connman(
        &method.to_string(),
        &servers,
        &excludes,
        &url.to_string(),
    ))
}

// ---- Clipboard ---------------------------------------------------------------------

pub fn clipboard_text() -> String {
    let s = cpp!(unsafe [] -> QString as "QString" {
        QClipboard *cb = QGuiApplication::clipboard();
        return cb ? cb->text() : QString();
    });
    s.to_string()
}

pub fn set_clipboard_text(text: &str) {
    let text = QString::from(text);
    cpp!(unsafe [text as "QString"] {
        if (QClipboard *cb = QGuiApplication::clipboard()) cb->setText(text);
    });
}

// ---- Image provider ----------------------------------------------------------------

thread_local! {
    static IMAGES: RefCell<HashMap<String, RgbaImage>> = RefCell::new(HashMap::new());
}

/// Publishes an image under `image://tuuli/<key>`.
pub fn set_image(key: &str, image: Option<RgbaImage>) {
    IMAGES.with(|m| {
        let mut m = m.borrow_mut();
        match image {
            Some(img) if !img.is_empty() => {
                m.insert(key.to_string(), img);
            }
            _ => {
                m.remove(key);
            }
        }
    });
}

pub fn remove_images_with_prefix(prefix: &str) {
    IMAGES.with(|m| m.borrow_mut().retain(|k, _| !k.starts_with(prefix)));
}

/// Copies the pixels for `id` into a fresh RGBA8 buffer; the C++ side
/// wraps it in a QImage copy.
fn image_for(id: &str) -> Option<RgbaImage> {
    // Ids look like "favicon/<tab>/<rev>"; the revision only busts caches.
    let key = id.rsplit_once('/').map(|(k, _)| k).unwrap_or(id);
    IMAGES.with(|m| m.borrow().get(key).cloned())
}

cpp! {{
    struct TuuliImageProvider : QQuickImageProvider {
        TuuliImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
        QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override {
            QByteArray utf8 = id.toUtf8();
            const char *idp = utf8.constData();
            unsigned w = 0, h = 0;
            unsigned char *pixels = rust!(Tuuli_imageRequest [idp: *const std::os::raw::c_char as "const char *", w: &mut u32 as "unsigned &", h: &mut u32 as "unsigned &"] -> *mut u8 as "unsigned char *" {
                let id = unsafe { std::ffi::CStr::from_ptr(idp) }.to_string_lossy().to_string();
                match image_for(&id) {
                    Some(img) => {
                        *w = img.width;
                        *h = img.height;
                        let mut data = img.data.into_boxed_slice();
                        let ptr = data.as_mut_ptr();
                        std::mem::forget(data);
                        ptr
                    }
                    None => std::ptr::null_mut(),
                }
            });
            QImage img;
            if (pixels && w > 0 && h > 0) {
                img = QImage(pixels, int(w), int(h), int(w) * 4, QImage::Format_RGBA8888).copy();
                rust!(Tuuli_imageFree [pixels: *mut u8 as "unsigned char *", w: u32 as "unsigned", h: u32 as "unsigned"] {
                    let len = (w * h * 4) as usize;
                    unsafe { drop(Box::from_raw(std::ptr::slice_from_raw_parts_mut(pixels, len))); }
                });
            } else {
                img = QImage(1, 1, QImage::Format_ARGB32);
                img.fill(Qt::transparent);
            }
            if (requestedSize.isValid() && !requestedSize.isEmpty())
                img = img.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            if (size) *size = img.size();
            return img;
        }
    };
}}

/// Registers `image://tuuli/` on a `QQmlEngine*`.
pub fn add_image_provider(qml_engine: *mut c_void) {
    cpp!(unsafe [qml_engine as "QQmlEngine *"] {
        if (qml_engine && !qml_engine->imageProvider(QStringLiteral("tuuli")))
            qml_engine->addImageProvider(QStringLiteral("tuuli"), new TuuliImageProvider());
    });
}

/// `QGuiApplication::applicationState()`: 4 == Qt::ApplicationActive.
pub fn application_is_active() -> bool {
    cpp!(unsafe [] -> bool as "bool" { return QGuiApplication::applicationState() == Qt::ApplicationActive; })
}
