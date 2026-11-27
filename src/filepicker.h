#pragma once

#include <QObject>
#include <QStringList>

// Abstract file picker interface (adapted from Omacut's filepicker.h pattern)
// so tests can inject a fake picker without touching D-Bus portals.
class FilePicker : public QObject {
    Q_OBJECT

public:
    explicit FilePicker(QObject *parent = nullptr) : QObject(parent) {}
    ~FilePicker() override = default;

    virtual void openFiles(const QString &title, bool multiple, const QString &initialDir = {}) = 0;
    virtual void openDirectory(const QString &title, const QString &initialDir = {}) = 0;

signals:
    void filesPicked(const QStringList &paths);
    void directoryPicked(const QString &path);
    void failed(const QString &message);
    void cancelled();
};
