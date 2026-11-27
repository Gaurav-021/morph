#include "backend.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QUuid>

int QueueModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_jobs.size();
}

QVariant QueueModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_jobs.size())
        return {};
    const QueueJob &job = m_jobs.at(index.row());
    switch (role) {
    case IdRole: return job.id;
    case SourcePathRole: return job.sourcePath;
    case FileNameRole: return job.fileName;
    case SourceExtRole: return job.sourceExt;
    case CategoryRole: return job.category;
    case FileSizeRole: return job.fileSize;
    case AvailableTargetsRole: return job.availableTargets;
    case TargetFormatRole: return job.targetFormat;
    case StatusRole: return job.status;
    case ProgressRole: return job.progress;
    case OutputPathRole: return job.outputPath;
    case NoteRole: return job.note;
    case ErrorRole: return job.error;
    default: return {};
    }
}

QHash<int, QByteArray> QueueModel::roleNames() const {
    return {
        {IdRole, "jobId"},
        {SourcePathRole, "sourcePath"},
        {FileNameRole, "fileName"},
        {SourceExtRole, "sourceExt"},
        {CategoryRole, "category"},
        {FileSizeRole, "fileSize"},
        {AvailableTargetsRole, "availableTargets"},
        {TargetFormatRole, "targetFormat"},
        {StatusRole, "status"},
        {ProgressRole, "progress"},
        {OutputPathRole, "outputPath"},
        {NoteRole, "note"},
        {ErrorRole, "error"}
    };
}

void QueueModel::setJobs(const QList<QueueJob> &jobs) {
    beginResetModel();
    m_jobs = jobs;
    endResetModel();
}

void QueueModel::appendJob(const QueueJob &job) {
    const int row = m_jobs.size();
    beginInsertRows(QModelIndex(), row, row);
    m_jobs.append(job);
    endInsertRows();
}

void QueueModel::removeJobAt(int row) {
    if (row < 0 || row >= m_jobs.size())
        return;
    beginRemoveRows(QModelIndex(), row, row);
    m_jobs.removeAt(row);
    endRemoveRows();
}

void QueueModel::updateJobAt(int row, const QueueJob &job) {
    if (row < 0 || row >= m_jobs.size())
        return;
    m_jobs[row] = job;
    const QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx);
}

QueueJob QueueModel::jobAt(int row) const {
    if (row < 0 || row >= m_jobs.size())
        return {};
    return m_jobs.at(row);
}

Backend::Backend(FilePicker *picker, const ToolRegistry &tools, QObject *parent)
    : QObject(parent), m_picker(picker), m_tools(tools) {
    if (m_picker) {
        connect(m_picker, &FilePicker::filesPicked, this, &Backend::addFiles);
        connect(m_picker, &FilePicker::directoryPicked, this, &Backend::setOutputDirectory);
        connect(m_picker, &FilePicker::failed, this, &Backend::setError);
    }

    const QStringList missing = m_tools.missingTools();
    if (!missing.isEmpty())
        setStatus(tr("Missing tools: %1").arg(missing.join(", ")));
}

Backend::~Backend() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    if (m_probeProcess && m_probeProcess->state() != QProcess::NotRunning) {
        m_probeProcess->kill();
        m_probeProcess->waitForFinished(500);
    }
    cleanupActiveScratch();
}

void Backend::setSelectedIndex(int index) {
    const int clamped = m_model.rowCount() == 0 ? -1 : qBound(0, index, m_model.rowCount() - 1);
    if (clamped != m_selectedIndex) {
        m_selectedIndex = clamped;
        emit selectionChanged();
    }
}

void Backend::setOutputDirectory(const QString &dir) {
    const QString clean = dir.trimmed().isEmpty() ? QString() : QDir(dir).absolutePath();
    if (clean != m_outputDirectory) {
        m_outputDirectory = clean;
        emit settingsChanged();
    }
}

void Backend::setQuality(const QString &quality) {
    if (quality != "high" && quality != "balanced" && quality != "compact")
        return;
    if (quality != m_quality) {
        m_quality = quality;
        emit settingsChanged();
    }
}

void Backend::setScale(const QString &scale) {
    if (scale != "original" && scale != "large" && scale != "medium" && scale != "small")
        return;
    if (scale != m_scale) {
        m_scale = scale;
        emit settingsChanged();
    }
}

int Backend::completedCount() const {
    int count = 0;
    for (const auto &job : m_model.jobs())
        if (job.status == "done")
            ++count;
    return count;
}

int Backend::failedCount() const {
    int count = 0;
    for (const auto &job : m_model.jobs())
        if (job.status == "error")
            ++count;
    return count;
}

bool Backend::hasPending() const {
    for (const auto &job : m_model.jobs())
        if (job.status == "pending" || job.status == "error" || job.status == "cancelled")
            return true;
    return false;
}

void Backend::pickFiles() {
    if (!m_picker)
        return;
    const QString initial = !m_outputDirectory.isEmpty() ? m_outputDirectory : QDir::homePath();
    m_picker->openFiles(tr("Add files to convert"), true, initial);
}

void Backend::pickOutputDirectory() {
    if (!m_picker)
        return;
    const QString initial = !m_outputDirectory.isEmpty() ? m_outputDirectory : QDir::homePath();
    m_picker->openDirectory(tr("Choose output folder"), initial);
}

void Backend::clearOutputDirectory() {
    setOutputDirectory({});
}

void Backend::addUrls(const QList<QUrl> &urls) {
    QStringList paths;
    for (const QUrl &url : urls) {
        if (url.isLocalFile())
            paths.append(url.toLocalFile());
    }
    if (!paths.isEmpty())
        addFiles(paths);
}

void Backend::addFiles(const QStringList &paths) {
    if (paths.isEmpty())
        return;

    QList<QueueJob> additions;
    QStringList unsupported;

    for (const QString &rawPath : paths) {
        const QFileInfo info(rawPath);
        if (!info.exists() || !info.isFile())
            continue;

        const QString absPath = info.absoluteFilePath();
        const QString ext = Converter::sourceExtension(absPath);
        const QStringList targets = Converter::targetFormatsForExtension(ext);
        if (targets.isEmpty()) {
            unsupported.append(info.fileName());
            continue;
        }

        QueueJob job;
        job.id = m_nextJobId++;
        job.sourcePath = absPath;
        job.fileName = info.fileName();
        job.sourceExt = ext;
        job.category = Converter::categoryForExtension(ext);
        job.fileSize = info.size();
        job.availableTargets = targets;
        job.targetFormat = Converter::defaultTargetForExtension(ext);
        additions.append(job);
    }

    if (!unsupported.isEmpty()) {
        setError(tr("Unsupported file type: %1").arg(unsupported.join(", ")));
    } else if (!additions.isEmpty()) {
        dismissError();
    }

    if (additions.isEmpty())
        return;

    pushUndoState();
    for (const QueueJob &job : additions)
        m_model.appendJob(job);

    if (m_selectedIndex < 0)
        setSelectedIndex(0);
    else
        setSelectedIndex(m_model.rowCount() - 1);

    setStatus(tr("Added %n files", "", additions.size()));
    emit queueChanged();
}

void Backend::removeAt(int index) {
    if (index < 0 || index >= m_model.rowCount())
        return;
    if (m_busy && index == m_activeRow) {
        setError(tr("Cannot remove an active conversion; cancel it first"));
        return;
    }

    pushUndoState();
    m_model.removeJobAt(index);
    if (m_busy && index < m_activeRow)
        --m_activeRow;

    if (m_model.rowCount() == 0)
        setSelectedIndex(-1);
    else
        setSelectedIndex(qMin(m_selectedIndex, m_model.rowCount() - 1));
    emit queueChanged();
}

void Backend::clearCompleted() {
    if (m_busy)
        return;
    QList<QueueJob> kept;
    for (const QueueJob &job : m_model.jobs()) {
        if (job.status != "done")
            kept.append(job);
    }
    if (kept.size() == m_model.rowCount())
        return;

    pushUndoState();
    m_model.setJobs(kept);
    setSelectedIndex(kept.isEmpty() ? -1 : qMin(m_selectedIndex, kept.size() - 1));
    emit queueChanged();
}

void Backend::clearAll() {
    if (m_busy) {
        setError(tr("Cancel the active conversion before clearing the queue"));
        return;
    }
    if (m_model.rowCount() == 0)
        return;
    pushUndoState();
    m_model.setJobs({});
    setSelectedIndex(-1);
    dismissError();
    setStatus({});
    emit queueChanged();
}

void Backend::setTargetFormat(int index, const QString &format) {
    if (index < 0 || index >= m_model.rowCount())
        return;
    QueueJob job = m_model.jobAt(index);
    if (!job.availableTargets.contains(format) || job.targetFormat == format)
        return;
    if (m_busy && index == m_activeRow)
        return;

    pushUndoState();
    job.targetFormat = format;
    if (job.status != "pending") {
        job.status = "pending";
        job.progress = 0.0;
        job.error.clear();
        job.note.clear();
    }
    m_model.updateJobAt(index, job);
    emit queueChanged();
}

void Backend::cycleTargetFormat(int index, int delta) {
    if (index < 0 || index >= m_model.rowCount())
        return;
    const QueueJob job = m_model.jobAt(index);
    if (job.availableTargets.isEmpty())
        return;
    const int cur = job.availableTargets.indexOf(job.targetFormat);
    const int next = (cur + delta + job.availableTargets.size()) % job.availableTargets.size();
    setTargetFormat(index, job.availableTargets.at(next));
}

void Backend::applyFormatToCategory(int sourceIndex) {
    if (sourceIndex < 0 || sourceIndex >= m_model.rowCount())
        return;
    const QueueJob sourceJob = m_model.jobAt(sourceIndex);
    bool changed = false;
    QList<QueueJob> jobs = m_model.jobs();
    for (int i = 0; i < jobs.size(); ++i) {
        if (i == sourceIndex || (m_busy && i == m_activeRow))
            continue;
        if (jobs[i].category == sourceJob.category && jobs[i].availableTargets.contains(sourceJob.targetFormat)
            && jobs[i].targetFormat != sourceJob.targetFormat) {
            if (!changed) {
                pushUndoState();
                changed = true;
            }
            jobs[i].targetFormat = sourceJob.targetFormat;
            if (jobs[i].status != "pending") {
                jobs[i].status = "pending";
                jobs[i].progress = 0.0;
                jobs[i].error.clear();
                jobs[i].note.clear();
            }
        }
    }
    if (changed) {
        m_model.setJobs(jobs);
        emit queueChanged();
        setStatus(tr("Applied .%1 to matching files").arg(sourceJob.targetFormat.toUpper()));
    }
}

void Backend::primaryAction() {
    if (m_busy) {
        cancelConversion();
        return;
    }
    if (m_model.rowCount() == 0) {
        pickFiles();
        return;
    }
    convertAll();
}

void Backend::convertAll() {
    if (m_busy || m_model.rowCount() == 0)
        return;

    QList<QueueJob> jobs = m_model.jobs();
    bool anyRunnable = false;
    for (auto &job : jobs) {
        if (job.status == "pending" || job.status == "error" || job.status == "cancelled") {
            job.status = "pending";
            job.progress = 0.0;
            job.error.clear();
            job.note.clear();
            anyRunnable = true;
        }
    }
    if (!anyRunnable) {
        setStatus(tr("All files are already converted"));
        return;
    }

    dismissError();
    m_cancelRequested = false;
    m_model.setJobs(jobs);
    m_busy = true;
    emit queueChanged();
    emit stateChanged();
    startNextPending();
}

void Backend::retryFailed() {
    if (m_busy)
        return;
    QList<QueueJob> jobs = m_model.jobs();
    bool any = false;
    for (auto &job : jobs) {
        if (job.status == "error" || job.status == "cancelled") {
            job.status = "pending";
            job.progress = 0.0;
            job.error.clear();
            any = true;
        }
    }
    if (!any)
        return;
    m_model.setJobs(jobs);
    convertAll();
}

void Backend::cancelConversion() {
    if (!m_busy)
        return;
    m_cancelRequested = true;
    setStatus(tr("Cancelling..."));
    if (m_process && m_process->state() != QProcess::NotRunning)
        m_process->kill();
}

void Backend::undo() {
    if (m_busy || m_undoStack.isEmpty())
        return;
    m_redoStack.append({m_model.jobs(), m_selectedIndex});
    const UndoSnapshot snap = m_undoStack.takeLast();
    m_model.setJobs(snap.jobs);
    setSelectedIndex(snap.selectedIndex);
    emit queueChanged();
    emit undoChanged();
}

void Backend::redo() {
    if (m_busy || m_redoStack.isEmpty())
        return;
    m_undoStack.append({m_model.jobs(), m_selectedIndex});
    const UndoSnapshot snap = m_redoStack.takeLast();
    m_model.setJobs(snap.jobs);
    setSelectedIndex(snap.selectedIndex);
    emit queueChanged();
    emit undoChanged();
}

void Backend::openOutputFolder(int index) {
    if (index < 0 || index >= m_model.rowCount())
        return;
    const QueueJob job = m_model.jobAt(index);
    const QString target = !job.outputPath.isEmpty() ? QFileInfo(job.outputPath).absolutePath()
                                                     : QFileInfo(job.sourcePath).absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(target));
}

void Backend::dismissError() {
    if (!m_errorMessage.isEmpty()) {
        m_errorMessage.clear();
        emit stateChanged();
    }
}

QString Backend::formatBytes(qint64 bytes) const {
    const QLocale locale;
    if (bytes < 1024)
        return tr("%1 B").arg(locale.toString(bytes));
    const double kb = bytes / 1024.0;
    if (kb < 1024.0)
        return tr("%1 KB").arg(locale.toString(kb, 'f', 1));
    const double mb = kb / 1024.0;
    if (mb < 1024.0)
        return tr("%1 MB").arg(locale.toString(mb, 'f', 1));
    return tr("%1 GB").arg(locale.toString(mb / 1024.0, 'f', 2));
}

QString Backend::categoryLabel(const QString &category) const {
    if (category == "video") return tr("Video");
    if (category == "audio") return tr("Audio");
    if (category == "image") return tr("Image");
    if (category == "document") return tr("Document");
    if (category == "spreadsheet") return tr("Spreadsheet");
    if (category == "presentation") return tr("Presentation");
    if (category == "pdf") return tr("PDF");
    if (category == "archive") return tr("Archive");
    return tr("File");
}

void Backend::pushUndoState() {
    m_undoStack.append({m_model.jobs(), m_selectedIndex});
    if (m_undoStack.size() > 40)
        m_undoStack.removeFirst();
    m_redoStack.clear();
    emit undoChanged();
}

void Backend::startNextPending() {
    if (m_cancelRequested) {
        m_busy = false;
        m_activeRow = -1;
        setStatus(tr("Conversion cancelled"));
        emit stateChanged();
        return;
    }

    int nextRow = -1;
    for (int i = 0; i < m_model.rowCount(); ++i) {
        if (m_model.jobAt(i).status == "pending") {
            nextRow = i;
            break;
        }
    }

    if (nextRow < 0) {
        m_busy = false;
        m_activeRow = -1;
        const int done = completedCount();
        const int failed = failedCount();
        if (failed > 0)
            setStatus(tr("Finished with %1 errors").arg(failed));
        else
            setStatus(tr("Converted %n files", "", done));
        emit stateChanged();
        return;
    }

    startSingleStepJob(nextRow);
}

void Backend::probeDurationAsync(const QString &sourcePath) {
    m_activeDurationSec = 0.0;
    const QString ffprobe = m_tools.find("ffprobe");
    if (ffprobe.isEmpty())
        return;
    if (!m_probeProcess) {
        m_probeProcess = new QProcess(this);
        connect(m_probeProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
            if (code == 0 && status == QProcess::NormalExit) {
                bool ok = false;
                const double dur = QString::fromUtf8(m_probeProcess->readAllStandardOutput()).trimmed().toDouble(&ok);
                if (ok && dur > 0.0)
                    m_activeDurationSec = dur;
            }
        });
    }
    if (m_probeProcess->state() != QProcess::NotRunning)
        m_probeProcess->kill();
    m_probeProcess->start(ffprobe, {"-v", "error", "-show_entries", "format=duration",
                                    "-of", "default=noprint_wrappers=1:nokey=1", sourcePath});
}

void Backend::startSingleStepJob(int row) {
    cleanupActiveScratch();
    m_activeRow = row;
    m_activeStage = 0;
    m_activeStderr.clear();

    QueueJob job = m_model.jobAt(row);
    const QString tool = Converter::requiredTool(job.sourceExt, job.targetFormat);
    const QString exe = m_tools.find(tool);
    if (exe.isEmpty()) {
        finishActiveJob(false, {}, {}, tr("Required tool '%1' is not installed").arg(tool));
        return;
    }

    const QString destDir = m_outputDirectory.isEmpty()
                                ? QFileInfo(job.sourcePath).absolutePath()
                                : QDir(m_outputDirectory).absolutePath();
    if (!QDir().mkpath(destDir)) {
        finishActiveJob(false, {}, {}, tr("Could not create output folder: %1").arg(destDir));
        return;
    }

    job.status = "running";
    job.progress = 0.05;
    job.error.clear();
    m_model.updateJobAt(row, job);
    setSelectedIndex(row);
    setStatus(tr("Converting %1...").arg(job.fileName));
    emit queueChanged();

    const ConversionOptions opts{m_outputDirectory, m_quality, m_scale};

    auto makeTempBeside = [&](const QString &ext) {
        return QDir(destDir).filePath(QString(".morph-tmp-%1.%2")
                                          .arg(QUuid::createUuid().toString(QUuid::Id128).left(8), ext));
    };

    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_process = new QProcess(this);
    connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        m_activeStderr.append(QString::fromUtf8(m_process->readAllStandardError()));
    });

    const QString cat = job.category;
    const QString dst = job.targetFormat;

    if (cat == "video" || cat == "audio") {
        probeDurationAsync(job.sourcePath);
        m_activeTempOut = makeTempBeside(dst);
        m_activeFinalOut = Converter::uniqueOutputPath(job.sourcePath, dst, m_outputDirectory);
        QStringList args = Converter::buildFfmpegArgs(job.sourcePath, m_activeTempOut, job.sourceExt, dst, opts);
        // Inject -progress pipe:1 after -y so stdout reports live out_time_us.
        const int yIdx = args.indexOf("-y");
        args.insert(yIdx + 1, "-progress");
        args.insert(yIdx + 2, "pipe:1");

        connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
            if (m_activeRow < 0 || m_activeDurationSec <= 0.0)
                return;
            const QString out = QString::fromUtf8(m_process->readAllStandardOutput());
            static const QRegularExpression re("out_time_us=(\\d+)");
            auto it = re.globalMatch(out);
            double lastSec = -1.0;
            while (it.hasNext()) {
                const auto m = it.next();
                lastSec = m.captured(1).toDouble() / 1000000.0;
            }
            if (lastSec >= 0.0) {
                QueueJob current = m_model.jobAt(m_activeRow);
                current.progress = qBound(0.05, lastSec / m_activeDurationSec, 0.98);
                m_model.updateJobAt(m_activeRow, current);
            }
        });

        connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
            if (m_cancelRequested || code != 0 || status != QProcess::NormalExit) {
                QFile::remove(m_activeTempOut);
                finishActiveJob(false, {}, {}, m_cancelRequested ? tr("Cancelled") : m_activeStderr.section('\n', -4).trimmed());
                return;
            }
            QString err;
            if (!Converter::atomicInstallFile(m_activeTempOut, m_activeFinalOut, &err)) {
                QFile::remove(m_activeTempOut);
                finishActiveJob(false, {}, {}, err);
                return;
            }
            finishActiveJob(true, m_activeFinalOut, {}, {});
        });

        m_process->start(exe, args);
        return;
    }

    if (cat == "image") {
        m_activeTempOut = makeTempBeside(dst);
        m_activeFinalOut = Converter::uniqueOutputPath(job.sourcePath, dst, m_outputDirectory);
        connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
            if (m_cancelRequested || code != 0 || status != QProcess::NormalExit) {
                QFile::remove(m_activeTempOut);
                finishActiveJob(false, {}, {}, m_cancelRequested ? tr("Cancelled") : m_activeStderr.section('\n', -4).trimmed());
                return;
            }
            QString err;
            if (!Converter::atomicInstallFile(m_activeTempOut, m_activeFinalOut, &err)) {
                QFile::remove(m_activeTempOut);
                finishActiveJob(false, {}, {}, err);
                return;
            }
            finishActiveJob(true, m_activeFinalOut, {}, {});
        });
        m_process->start(exe, Converter::buildMagickArgs(job.sourcePath, m_activeTempOut, job.sourceExt, dst, opts));
        return;
    }

    if (cat == "document" || cat == "spreadsheet" || cat == "presentation") {
        m_stageDir = new QTemporaryDir();
        m_profileDir = new QTemporaryDir();
        if (!m_stageDir->isValid() || !m_profileDir->isValid()) {
            finishActiveJob(false, {}, {}, tr("Could not create temporary staging directory"));
            return;
        }
        const QStringList args = Converter::buildLibreOfficeArgs(
            job.sourcePath, m_profileDir->path(), m_stageDir->path(), job.sourceExt, dst);
        connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
            const QueueJob cur = m_model.jobAt(m_activeRow);
            if (m_cancelRequested || code != 0 || status != QProcess::NormalExit) {
                finishActiveJob(false, {}, {}, m_cancelRequested ? tr("Cancelled") : m_activeStderr.section('\n', -4).trimmed());
                return;
            }
            QDir stage(m_stageDir->path());
            const QStringList produced = stage.entryList({"*." + cur.targetFormat}, QDir::Files);
            if (produced.isEmpty()) {
                finishActiveJob(false, {}, {}, tr("LibreOffice did not produce a .%1 file").arg(cur.targetFormat));
                return;
            }
            const QString finalOut = Converter::uniqueOutputPath(cur.sourcePath, cur.targetFormat, m_outputDirectory);
            QString err;
            if (!Converter::atomicInstallFile(stage.filePath(produced.first()), finalOut, &err)) {
                finishActiveJob(false, {}, {}, err);
                return;
            }
            finishActiveJob(true, finalOut, {}, {});
        });
        m_process->start(exe, args);
        return;
    }

    if (cat == "pdf") {
        if (dst == "pdf" || dst == "txt") {
            m_activeTempOut = makeTempBeside(dst);
            m_activeFinalOut = Converter::uniqueOutputPath(job.sourcePath, dst, m_outputDirectory,
                                                           dst == "pdf" ? " (compressed)" : QString());
            const QStringList args = (dst == "pdf")
                                         ? Converter::buildGhostscriptArgs(job.sourcePath, m_activeTempOut, opts)
                                         : QStringList{job.sourcePath, m_activeTempOut};
            connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
                if (m_cancelRequested || code != 0 || status != QProcess::NormalExit) {
                    QFile::remove(m_activeTempOut);
                    finishActiveJob(false, {}, {}, m_cancelRequested ? tr("Cancelled") : m_activeStderr.section('\n', -4).trimmed());
                    return;
                }
                QString err;
                if (!Converter::atomicInstallFile(m_activeTempOut, m_activeFinalOut, &err)) {
                    QFile::remove(m_activeTempOut);
                    finishActiveJob(false, {}, {}, err);
                    return;
                }
                finishActiveJob(true, m_activeFinalOut, {}, {});
            });
            m_process->start(exe, args);
            return;
        }

        // PDF -> images
        if (dst == "webp" && !m_tools.has("magick")) {
            finishActiveJob(false, {}, {}, tr("Required tool 'magick' is not installed"));
            return;
        }
        m_stageDir = new QTemporaryDir();
        if (!m_stageDir->isValid()) {
            finishActiveJob(false, {}, {}, tr("Could not create temporary staging directory"));
            return;
        }
        const int dpi = (m_quality == "high") ? 200 : (m_quality == "compact" ? 100 : 150);
        const QString ppmFmt = (dst == "jpg") ? "-jpeg" : "-png";
        const QString ppmExt = (dst == "jpg") ? "jpg" : "png";

        connect(m_process, &QProcess::finished, this, [this, ppmExt](int code, QProcess::ExitStatus status) {
            const QueueJob cur = m_model.jobAt(m_activeRow);
            if (m_cancelRequested || code != 0 || status != QProcess::NormalExit) {
                finishActiveJob(false, {}, {}, m_cancelRequested ? tr("Cancelled") : m_activeStderr.section('\n', -4).trimmed());
                return;
            }
            QDir stage(m_stageDir->path());
            if (m_activeStage == 0) {
                m_stagePages = stage.entryList({"page-*." + ppmExt}, QDir::Files, QDir::Name);
                if (m_stagePages.isEmpty()) {
                    finishActiveJob(false, {}, {}, tr("No pages were rendered from PDF"));
                    return;
                }
                if (cur.targetFormat == "webp") {
                    m_activeStage = 1;
                    m_stagePageIndex = 0;
                    const ConversionOptions cOpts{m_outputDirectory, m_quality, m_scale};
                    const QString srcPage = stage.filePath(m_stagePages.at(0));
                    const QString webpStage = stage.filePath("out-001.webp");
                    m_process->start(m_tools.find("magick"),
                                     Converter::buildMagickArgs(srcPage, webpStage, "png", "webp", cOpts));
                    return;
                }
                // Install rendered png/jpg pages atomically
                QStringList installed;
                for (int i = 0; i < m_stagePages.size(); ++i) {
                    const QString suffix = m_stagePages.size() == 1
                                               ? QString()
                                               : QString("-page-%1").arg(i + 1, 2, 10, QChar('0'));
                    const QString finalPage = Converter::uniqueOutputPath(cur.sourcePath, cur.targetFormat,
                                                                          m_outputDirectory, suffix);
                    QString err;
                    if (!Converter::atomicInstallFile(stage.filePath(m_stagePages.at(i)), finalPage, &err)) {
                        finishActiveJob(false, {}, {}, err);
                        return;
                    }
                    installed.append(finalPage);
                }
                const QString note = installed.size() > 1 ? tr("%n pages", "", installed.size()) : QString();
                finishActiveJob(true, installed.first(), note, {});
                return;
            }

            // Stage 1: converting each page to webp
            ++m_stagePageIndex;
            if (m_stagePageIndex < m_stagePages.size()) {
                QueueJob progJob = m_model.jobAt(m_activeRow);
                progJob.progress = 0.2 + 0.7 * (static_cast<double>(m_stagePageIndex) / m_stagePages.size());
                m_model.updateJobAt(m_activeRow, progJob);

                const ConversionOptions cOpts{m_outputDirectory, m_quality, m_scale};
                const QString srcPage = stage.filePath(m_stagePages.at(m_stagePageIndex));
                const QString webpStage = stage.filePath(QString("out-%1.webp").arg(m_stagePageIndex + 1, 3, 10, QChar('0')));
                m_process->start(m_tools.find("magick"),
                                 Converter::buildMagickArgs(srcPage, webpStage, "png", "webp", cOpts));
                return;
            }

            QStringList installed;
            for (int i = 0; i < m_stagePages.size(); ++i) {
                const QString webpStage = stage.filePath(QString("out-%1.webp").arg(i + 1, 3, 10, QChar('0')));
                const QString suffix = m_stagePages.size() == 1
                                           ? QString()
                                           : QString("-page-%1").arg(i + 1, 2, 10, QChar('0'));
                const QString finalPage = Converter::uniqueOutputPath(cur.sourcePath, "webp",
                                                                      m_outputDirectory, suffix);
                QString err;
                if (!Converter::atomicInstallFile(webpStage, finalPage, &err)) {
                    finishActiveJob(false, {}, {}, err);
                    return;
                }
                installed.append(finalPage);
            }
            const QString note = installed.size() > 1 ? tr("%n pages", "", installed.size()) : QString();
            finishActiveJob(true, installed.first(), note, {});
        });

        m_process->start(exe, {ppmFmt, "-r", QString::number(dpi), job.sourcePath, m_stageDir->filePath("page")});
        return;
    }

    if (cat == "archive") {
        m_stageDir = new QTemporaryDir();
        if (!m_stageDir->isValid()) {
            finishActiveJob(false, {}, {}, tr("Could not create temporary staging directory"));
            return;
        }
        m_activeTempOut = makeTempBeside(dst);
        m_activeFinalOut = Converter::uniqueOutputPath(job.sourcePath, dst, m_outputDirectory);

        const bool srcIsTar = job.sourceExt.startsWith("tar") || job.sourceExt == "tgz"
                              || job.sourceExt == "txz" || job.sourceExt == "tbz2";
        QString extractExe;
        QStringList extractArgs;
        if (srcIsTar && m_tools.has("tar")) {
            extractExe = m_tools.find("tar");
            extractArgs = {"-xf", job.sourcePath, "-C", m_stageDir->path()};
        } else if (m_tools.has("7z")) {
            extractExe = m_tools.find("7z");
            extractArgs = {"x", "-y", QString("-o%1").arg(m_stageDir->path()), job.sourcePath};
        } else {
            finishActiveJob(false, {}, {}, tr("Required tool '7z' is not installed"));
            return;
        }

        connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
            const QueueJob cur = m_model.jobAt(m_activeRow);
            if (m_cancelRequested || code != 0 || status != QProcess::NormalExit) {
                QFile::remove(m_activeTempOut);
                finishActiveJob(false, {}, {}, m_cancelRequested ? tr("Cancelled") : m_activeStderr.section('\n', -4).trimmed());
                return;
            }
            if (m_activeStage == 0) {
                m_activeStage = 1;
                QueueJob prog = cur;
                prog.progress = 0.5;
                m_model.updateJobAt(m_activeRow, prog);

                const QString packTool = Converter::requiredTool(cur.sourceExt, cur.targetFormat);
                const QString packExe = m_tools.find(packTool);
                if (packExe.isEmpty()) {
                    finishActiveJob(false, {}, {}, tr("Required tool '%1' is not installed").arg(packTool));
                    return;
                }
                if (packTool == "tar") {
                    m_process->setWorkingDirectory({});
                    m_process->start(packExe, {"-caf", m_activeTempOut, "-C", m_stageDir->path(), "."});
                } else {
                    m_process->setWorkingDirectory(m_stageDir->path());
                    m_process->start(packExe, {"a", "-y", m_activeTempOut, "."});
                }
                return;
            }

            QString err;
            if (!Converter::atomicInstallFile(m_activeTempOut, m_activeFinalOut, &err)) {
                QFile::remove(m_activeTempOut);
                finishActiveJob(false, {}, {}, err);
                return;
            }
            finishActiveJob(true, m_activeFinalOut, {}, {});
        });

        m_process->start(extractExe, extractArgs);
        return;
    }

    finishActiveJob(false, {}, {}, tr("Unsupported conversion"));
}

void Backend::finishActiveJob(bool ok, const QString &outputPath, const QString &note, const QString &error) {
    if (m_activeRow >= 0 && m_activeRow < m_model.rowCount()) {
        QueueJob job = m_model.jobAt(m_activeRow);
        if (m_cancelRequested) {
            job.status = "cancelled";
            job.progress = 0.0;
            job.error.clear();
        } else if (ok) {
            job.status = "done";
            job.progress = 1.0;
            job.outputPath = outputPath;
            job.note = note;
            job.error.clear();
        } else {
            job.status = "error";
            job.progress = 0.0;
            job.error = error.isEmpty() ? tr("Conversion failed") : error;
        }
        m_model.updateJobAt(m_activeRow, job);
        emit queueChanged();
    }

    cleanupActiveScratch();
    startNextPending();
}

void Backend::cleanupActiveScratch() {
    if (m_stageDir) {
        delete m_stageDir;
        m_stageDir = nullptr;
    }
    if (m_profileDir) {
        delete m_profileDir;
        m_profileDir = nullptr;
    }
    m_stagePages.clear();
    m_stagePageIndex = 0;
    m_activeTempOut.clear();
    m_activeFinalOut.clear();
    m_activeDurationSec = 0.0;
}

void Backend::setError(const QString &message) {
    m_errorMessage = message;
    emit stateChanged();
}

void Backend::setStatus(const QString &message) {
    m_statusMessage = message;
    emit stateChanged();
}
