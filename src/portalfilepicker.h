#pragma once

#include "filepicker.h"

#include <QVariantMap>

// Opens native portal file/folder dialogs via org.freedesktop.portal.FileChooser
// over D-Bus (pattern adapted from Omacut, MIT, David Heinemeier Hansson).
class PortalFilePicker : public FilePicker {
    Q_OBJECT

public:
    explicit PortalFilePicker(QObject *parent = nullptr);

    void openFiles(const QString &title, bool multiple, const QString &initialDir = {}) override;
    void openDirectory(const QString &title, const QString &initialDir = {}) override;

private slots:
    void onPortalResponse(uint response, const QVariantMap &results);

private:
    void requestOpen(const QString &title, bool multiple, bool directory, const QString &initialDir);

    QString m_activeRequestPath;
    bool m_pickingDirectory = false;
    int m_counter = 0;
};
