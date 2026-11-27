#pragma once

#include "converter.h"
#include "filepicker.h"

#include <QAbstractListModel>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QUrl>

struct QueueJob {
    int id = 0;
    QString sourcePath;
    QString fileName;
    QString sourceExt;
    QString category;
    qint64 fileSize = 0;
    QStringList availableTargets;
    QString targetFormat;
    QString status = "pending";   // "pending", "running", "done", "error", "cancelled"
    double progress = 0.0;
    QString outputPath;
    QString note;
    QString error;
};

class QueueModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        SourcePathRole,
        FileNameRole,
        SourceExtRole,
        CategoryRole,
        FileSizeRole,
        AvailableTargetsRole,
        TargetFormatRole,
        StatusRole,
        ProgressRole,
        OutputPathRole,
        NoteRole,
        ErrorRole
    };

    explicit QueueModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    const QList<QueueJob> &jobs() const { return m_jobs; }
    void setJobs(const QList<QueueJob> &jobs);
    void appendJob(const QueueJob &job);
    void removeJobAt(int row);
    void updateJobAt(int row, const QueueJob &job);
    QueueJob jobAt(int row) const;

private:
    QList<QueueJob> m_jobs;
};

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *queue READ queue CONSTANT)
    Q_PROPERTY(int count READ count NOTIFY queueChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex WRITE setSelectedIndex NOTIFY selectionChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString outputDirectory READ outputDirectory WRITE setOutputDirectory NOTIFY settingsChanged)
    Q_PROPERTY(QString quality READ quality WRITE setQuality NOTIFY settingsChanged)
    Q_PROPERTY(QString scale READ scale WRITE setScale NOTIFY settingsChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY stateChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
    Q_PROPERTY(QStringList missingTools READ missingTools NOTIFY toolsChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoChanged)
    Q_PROPERTY(int completedCount READ completedCount NOTIFY queueChanged)
    Q_PROPERTY(int failedCount READ failedCount NOTIFY queueChanged)
    Q_PROPERTY(bool hasPending READ hasPending NOTIFY queueChanged)

public:
    explicit Backend(FilePicker *picker = nullptr, const ToolRegistry &tools = ToolRegistry::detect(),
                     QObject *parent = nullptr);
    ~Backend() override;

    QObject *queue() { return &m_model; }
    int count() const { return m_model.rowCount(); }
    int selectedIndex() const { return m_selectedIndex; }
    void setSelectedIndex(int index);

    bool busy() const { return m_busy; }
    QString outputDirectory() const { return m_outputDirectory; }
    void setOutputDirectory(const QString &dir);
    QString quality() const { return m_quality; }
    void setQuality(const QString &quality);
    QString scale() const { return m_scale; }
    void setScale(const QString &scale);

    QString statusMessage() const { return m_statusMessage; }
    QString errorMessage() const { return m_errorMessage; }
    QStringList missingTools() const { return m_tools.missingTools(); }
    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }
    int completedCount() const;
    int failedCount() const;
    bool hasPending() const;

    Q_INVOKABLE void pickFiles();
    Q_INVOKABLE void pickOutputDirectory();
    Q_INVOKABLE void clearOutputDirectory();
    Q_INVOKABLE void addFiles(const QStringList &paths);
    Q_INVOKABLE void addUrls(const QList<QUrl> &urls);
    Q_INVOKABLE void removeAt(int index);
    Q_INVOKABLE void clearCompleted();
    Q_INVOKABLE void clearAll();
    Q_INVOKABLE void setTargetFormat(int index, const QString &format);
    Q_INVOKABLE void cycleTargetFormat(int index, int delta);
    Q_INVOKABLE void applyFormatToCategory(int sourceIndex);
    Q_INVOKABLE void primaryAction();
    Q_INVOKABLE void convertAll();
    Q_INVOKABLE void cancelConversion();
    Q_INVOKABLE void retryFailed();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void openOutputFolder(int index);
    Q_INVOKABLE void dismissError();
    Q_INVOKABLE QString formatBytes(qint64 bytes) const;
    Q_INVOKABLE QString categoryLabel(const QString &category) const;

signals:
    void queueChanged();
    void selectionChanged();
    void stateChanged();
    void settingsChanged();
    void toolsChanged();
    void undoChanged();

private:
    struct UndoSnapshot {
        QList<QueueJob> jobs;
        int selectedIndex = -1;
    };

    void pushUndoState();
    void startNextPending();
    void startSingleStepJob(int row);
    void probeDurationAsync(const QString &sourcePath);
    void finishActiveJob(bool ok, const QString &outputPath, const QString &note, const QString &error);
    void cleanupActiveScratch();
    void setError(const QString &message);
    void setStatus(const QString &message);

    QueueModel m_model;
    FilePicker *m_picker = nullptr;
    ToolRegistry m_tools;
    int m_nextJobId = 1;
    int m_selectedIndex = -1;

    bool m_busy = false;
    bool m_cancelRequested = false;
    int m_activeRow = -1;
    int m_activeStage = 0; // Used for multi-step jobs (archive extract -> pack, pdf -> webp).

    QString m_outputDirectory;
    QString m_quality = "balanced";
    QString m_scale = "original";
    QString m_statusMessage;
    QString m_errorMessage;

    QList<UndoSnapshot> m_undoStack;
    QList<UndoSnapshot> m_redoStack;

    QProcess *m_process = nullptr;
    QProcess *m_probeProcess = nullptr;
    double m_activeDurationSec = 0.0;
    QString m_activeTempOut;
    QString m_activeFinalOut;
    QString m_activeStderr;
    QTemporaryDir *m_stageDir = nullptr;
    QTemporaryDir *m_profileDir = nullptr;
    QStringList m_stagePages;
    int m_stagePageIndex = 0;
};
