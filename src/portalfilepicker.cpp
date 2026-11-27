// PortalFilePicker pattern adapted from Omacut (MIT, David Heinemeier Hansson).
#include "portalfilepicker.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QUrl>

PortalFilePicker::PortalFilePicker(QObject *parent) : FilePicker(parent) {}

void PortalFilePicker::openFiles(const QString &title, bool multiple, const QString &initialDir) {
    requestOpen(title, multiple, false, initialDir);
}

void PortalFilePicker::openDirectory(const QString &title, const QString &initialDir) {
    requestOpen(title, false, true, initialDir);
}

void PortalFilePicker::requestOpen(const QString &title, bool multiple, bool directory, const QString &initialDir) {
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        emit failed(tr("D-Bus session bus is unavailable"));
        return;
    }

    if (!m_activeRequestPath.isEmpty()) {
        bus.disconnect("org.freedesktop.portal.Desktop", m_activeRequestPath,
                       "org.freedesktop.portal.Request", "Response",
                       this, SLOT(onPortalResponse(uint,QVariantMap)));
        m_activeRequestPath.clear();
    }

    m_pickingDirectory = directory;
    const QString token = QString("morph_%1_%2").arg(QCoreApplication::applicationPid()).arg(++m_counter);
    QString sender = bus.baseService();
    if (sender.startsWith(':'))
        sender.remove(0, 1);
    sender.replace('.', '_');
    const QString expectedPath = QString("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);

    bus.connect("org.freedesktop.portal.Desktop", expectedPath,
                "org.freedesktop.portal.Request", "Response",
                this, SLOT(onPortalResponse(uint,QVariantMap)));
    m_activeRequestPath = expectedPath;

    QVariantMap options;
    options.insert("handle_token", token);
    options.insert("multiple", multiple);
    options.insert("directory", directory);
    if (!initialDir.isEmpty()) {
        QByteArray folderBytes = initialDir.toUtf8();
        folderBytes.append('\0');
        options.insert("current_folder", folderBytes);
    }

    QDBusInterface portal("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                          "org.freedesktop.portal.FileChooser", bus);
    QDBusReply<QDBusObjectPath> reply = portal.call("OpenFile", QString(), title, options);
    if (!reply.isValid()) {
        bus.disconnect("org.freedesktop.portal.Desktop", m_activeRequestPath,
                       "org.freedesktop.portal.Request", "Response",
                       this, SLOT(onPortalResponse(uint,QVariantMap)));
        m_activeRequestPath.clear();
        emit failed(tr("Could not open file chooser portal: %1").arg(reply.error().message()));
        return;
    }

    const QString actualPath = reply.value().path();
    if (!actualPath.isEmpty() && actualPath != expectedPath) {
        bus.disconnect("org.freedesktop.portal.Desktop", expectedPath,
                       "org.freedesktop.portal.Request", "Response",
                       this, SLOT(onPortalResponse(uint,QVariantMap)));
        m_activeRequestPath = actualPath;
        bus.connect("org.freedesktop.portal.Desktop", actualPath,
                    "org.freedesktop.portal.Request", "Response",
                    this, SLOT(onPortalResponse(uint,QVariantMap)));
    }
}

void PortalFilePicker::onPortalResponse(uint response, const QVariantMap &results) {
    auto bus = QDBusConnection::sessionBus();
    if (!m_activeRequestPath.isEmpty()) {
        bus.disconnect("org.freedesktop.portal.Desktop", m_activeRequestPath,
                       "org.freedesktop.portal.Request", "Response",
                       this, SLOT(onPortalResponse(uint,QVariantMap)));
        m_activeRequestPath.clear();
    }

    if (response != 0) {
        emit cancelled();
        return;
    }

    const QStringList uris = results.value("uris").toStringList();
    QStringList paths;
    paths.reserve(uris.size());
    for (const QString &uri : uris) {
        const QUrl url(uri);
        const QString local = url.isLocalFile() ? url.toLocalFile() : uri;
        if (!local.isEmpty())
            paths.append(local);
    }

    if (paths.isEmpty()) {
        emit cancelled();
        return;
    }

    if (m_pickingDirectory)
        emit directoryPicked(paths.first());
    else
        emit filesPicked(paths);
}
