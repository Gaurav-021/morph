#include "converter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <algorithm>

namespace {

const QStringList kVideoExts = {"mp4", "mkv", "webm", "mov", "avi", "flv", "wmv", "m4v", "ts", "mpg", "mpeg", "3gp", "ogv"};
const QStringList kAudioExts = {"mp3", "wav", "flac", "m4a", "aac", "ogg", "opus", "wma", "aiff", "alac", "amr", "mka"};
const QStringList kImageExts = {"png", "jpg", "jpeg", "webp", "avif", "gif", "tiff", "tif", "bmp", "ico", "heic", "heif", "svg", "tga", "ppm", "psd"};
const QStringList kDocExts = {"docx", "doc", "odt", "rtf", "txt", "html", "htm", "md"};
const QStringList kSheetExts = {"xlsx", "xls", "ods", "csv", "tsv"};
const QStringList kSlideExts = {"pptx", "ppt", "odp"};
const QStringList kArchiveExts = {"zip", "7z", "tar", "tar.gz", "tgz", "tar.xz", "txz", "tar.bz2", "tbz2", "gz", "bz2", "xz", "rar", "iso"};

const QStringList kKnownTools = {"ffmpeg", "ffprobe", "magick", "libreoffice", "pdftoppm", "pdftotext", "gs", "7z", "tar"};

QString normalizeExt(QString ext) {
    ext = ext.trimmed().toLower();
    if (ext.startsWith('.'))
        ext.remove(0, 1);
    return ext;
}

bool isTarVariant(const QString &ext) {
    return ext == "tar" || ext == "tar.gz" || ext == "tar.xz" || ext == "tar.bz2";
}

QString libreOfficeFormatName(const QString &ext) {
    if (ext == "txt")
        return "txt:Text";
    return ext;
}

} // namespace

ToolRegistry ToolRegistry::detect() {
    ToolRegistry reg;
    for (const QString &tool : kKnownTools) {
        QString found = QStandardPaths::findExecutable(tool);
        if (found.isEmpty() && tool == "libreoffice")
            found = QStandardPaths::findExecutable("soffice");
        reg.paths.insert(tool, found);
    }
    return reg;
}

QStringList ToolRegistry::missingTools() const {
    QStringList missing;
    for (const QString &tool : kKnownTools) {
        if (paths.value(tool).isEmpty())
            missing.append(tool);
    }
    return missing;
}

QString Converter::sourceExtension(const QString &filePath) {
    const QString name = QFileInfo(filePath).fileName().toLower();
    for (const char *compound : {"tar.gz", "tar.xz", "tar.bz2"}) {
        if (name.endsWith(QStringLiteral(".") + compound))
            return QString::fromLatin1(compound);
    }
    return QFileInfo(filePath).suffix().toLower();
}

QString Converter::baseStem(const QString &filePath) {
    const QString name = QFileInfo(filePath).fileName();
    const QString lower = name.toLower();
    for (const char *compound : {"tar.gz", "tar.xz", "tar.bz2"}) {
        const QString c = QString::fromLatin1(compound);
        if (lower.endsWith("." + c))
            return name.left(name.size() - c.size() - 1);
    }
    return QFileInfo(filePath).completeBaseName();
}

QString Converter::categoryForExtension(const QString &ext) {
    const QString e = normalizeExt(ext);
    if (kVideoExts.contains(e)) return "video";
    if (kAudioExts.contains(e)) return "audio";
    if (kImageExts.contains(e)) return "image";
    if (kDocExts.contains(e)) return "document";
    if (kSheetExts.contains(e)) return "spreadsheet";
    if (kSlideExts.contains(e)) return "presentation";
    if (e == "pdf") return "pdf";
    if (kArchiveExts.contains(e)) return "archive";
    return "unknown";
}

QStringList Converter::targetFormatsForExtension(const QString &ext) {
    const QString cat = categoryForExtension(ext);
    if (cat == "video")
        return {"mp4", "mkv", "webm", "mov", "gif", "mp3", "m4a", "flac", "wav", "opus"};
    if (cat == "audio")
        return {"mp3", "m4a", "flac", "wav", "opus", "ogg", "aac"};
    if (cat == "image")
        return {"png", "jpg", "webp", "avif", "gif", "tiff", "bmp", "ico", "pdf"};
    if (cat == "document")
        return {"pdf", "docx", "odt", "rtf", "txt", "html"};
    if (cat == "spreadsheet")
        return {"pdf", "xlsx", "ods", "csv"};
    if (cat == "presentation")
        return {"pdf", "pptx", "odp"};
    if (cat == "pdf")
        return {"png", "jpg", "webp", "txt", "pdf"};
    if (cat == "archive")
        return {"zip", "7z", "tar", "tar.gz", "tar.xz", "tar.bz2"};
    return {};
}

QString Converter::defaultTargetForExtension(const QString &ext) {
    const QString e = normalizeExt(ext);
    const QString cat = categoryForExtension(e);
    if (cat == "video")
        return (e == "mp4") ? "mkv" : "mp4";
    if (cat == "audio")
        return (e == "mp3") ? "m4a" : "mp3";
    if (cat == "image") {
        if (e == "png") return "jpg";
        if (e == "jpg" || e == "jpeg") return "webp";
        return "png";
    }
    if (cat == "document" || cat == "spreadsheet" || cat == "presentation")
        return (e == "pdf") ? "txt" : "pdf";
    if (cat == "pdf")
        return "png";
    if (cat == "archive")
        return (e == "zip") ? "7z" : "zip";
    return {};
}

QString Converter::requiredTool(const QString &sourceExt, const QString &targetExt) {
    const QString src = normalizeExt(sourceExt);
    const QString dst = normalizeExt(targetExt);
    const QString cat = categoryForExtension(src);

    if (cat == "video" || cat == "audio")
        return "ffmpeg";
    if (cat == "image")
        return "magick";
    if (cat == "document" || cat == "spreadsheet" || cat == "presentation")
        return "libreoffice";
    if (cat == "pdf") {
        if (dst == "txt") return "pdftotext";
        if (dst == "pdf") return "gs";
        return "pdftoppm";
    }
    if (cat == "archive")
        return isTarVariant(dst) ? "tar" : "7z";
    return {};
}

QString Converter::uniqueOutputPath(const QString &sourcePath, const QString &targetExt,
                                    const QString &outputDirectory, const QString &suffixHint) {
    const QFileInfo srcInfo(sourcePath);
    const QString dirPath = outputDirectory.isEmpty() ? srcInfo.absolutePath() : QDir(outputDirectory).absolutePath();
    const QString stem = baseStem(sourcePath) + suffixHint;
    const QString ext = normalizeExt(targetExt);

    QString candidate = QDir(dirPath).filePath(stem + "." + ext);
    if (!QFileInfo::exists(candidate) && QFileInfo(candidate).absoluteFilePath() != srcInfo.absoluteFilePath())
        return candidate;

    for (int i = 1; i < 10000; ++i) {
        candidate = QDir(dirPath).filePath(QString("%1 (%2).%3").arg(stem).arg(i).arg(ext));
        if (!QFileInfo::exists(candidate) && QFileInfo(candidate).absoluteFilePath() != srcInfo.absoluteFilePath())
            return candidate;
    }
    return QDir(dirPath).filePath(stem + "-" + QUuid::createUuid().toString(QUuid::Id128).left(8) + "." + ext);
}

QStringList Converter::buildFfmpegArgs(const QString &inputPath, const QString &tempOutputPath,
                                       const QString &sourceExt, const QString &targetExt,
                                       const ConversionOptions &options) {
    const QString srcCat = categoryForExtension(sourceExt);
    const QString dst = normalizeExt(targetExt);
    const QString dstCat = categoryForExtension(dst);

    QStringList args{"-hide_banner", "-y", "-i", inputPath};

    if (dst == "gif") {
        int width = 720;
        if (options.quality == "high") width = 960;
        else if (options.quality == "compact") width = 480;
        if (options.scale == "large") width = std::min(width, 1280);
        else if (options.scale == "medium") width = std::min(width, 720);
        else if (options.scale == "small") width = std::min(width, 480);

        args << "-vf" << QString("fps=15,scale='min(%1,iw)':-1:flags=lanczos,split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse").arg(width)
             << "-loop" << "0"
             << tempOutputPath;
        return args;
    }

    if (dstCat == "audio") {
        if (srcCat == "video")
            args << "-vn";
        QString bitrate = "192k";
        if (options.quality == "high") bitrate = "320k";
        else if (options.quality == "compact") bitrate = "128k";

        if (dst == "mp3") {
            args << "-c:a" << "libmp3lame" << "-b:a" << bitrate;
        } else if (dst == "m4a" || dst == "aac") {
            args << "-c:a" << "aac" << "-b:a" << (options.quality == "high" ? "256k" : bitrate);
        } else if (dst == "opus") {
            QString opusRate = options.quality == "high" ? "192k" : (options.quality == "compact" ? "96k" : "128k");
            args << "-c:a" << "libopus" << "-b:a" << opusRate;
        } else if (dst == "ogg") {
            QString q = options.quality == "high" ? "7" : (options.quality == "compact" ? "3" : "5");
            args << "-c:a" << "libvorbis" << "-q:a" << q;
        } else if (dst == "flac") {
            args << "-c:a" << "flac";
        } else if (dst == "wav") {
            args << "-c:a" << "pcm_s16le";
        }
        args << tempOutputPath;
        return args;
    }

    // Video output
    int heightLimit = 0;
    if (options.scale == "large") heightLimit = 1080;
    else if (options.scale == "medium") heightLimit = 720;
    else if (options.scale == "small") heightLimit = 480;

    if (heightLimit > 0)
        args << "-vf" << QString("scale=-2:'min(%1,ih)'").arg(heightLimit);

    if (dst == "webm") {
        QString crf = options.quality == "high" ? "24" : (options.quality == "compact" ? "37" : "31");
        args << "-c:v" << "libvpx-vp9" << "-crf" << crf << "-b:v" << "0"
             << "-c:a" << "libopus" << "-b:a" << "128k";
    } else {
        QString crf = options.quality == "high" ? "18" : (options.quality == "compact" ? "28" : "23");
        QString aBitrate = options.quality == "high" ? "192k" : (options.quality == "compact" ? "128k" : "160k");
        args << "-c:v" << "libx264" << "-preset" << "medium" << "-crf" << crf
             << "-pix_fmt" << "yuv420p"
             << "-c:a" << "aac" << "-b:a" << aBitrate;
        if (dst == "mp4" || dst == "mov")
            args << "-movflags" << "+faststart";
    }

    args << tempOutputPath;
    return args;
}

QStringList Converter::buildMagickArgs(const QString &inputPath, const QString &tempOutputPath,
                                       const QString &sourceExt, const QString &targetExt,
                                       const ConversionOptions &options) {
    const QString src = normalizeExt(sourceExt);
    const QString dst = normalizeExt(targetExt);

    QStringList args;
    if (src == "svg")
        args << "-background" << "none" << "-density" << "192";

    args << inputPath << "-auto-orient";

    int maxDim = 0;
    if (options.scale == "large") maxDim = 1920;
    else if (options.scale == "medium") maxDim = 1280;
    else if (options.scale == "small") maxDim = 800;

    if (maxDim > 0 && dst != "ico")
        args << "-resize" << QString("%1x%1>").arg(maxDim);

    if (dst == "ico") {
        args << "-define" << "icon:auto-resize=256,128,64,48,32,16";
    } else if (dst == "jpg" || dst == "jpeg") {
        args << "-background" << "white" << "-alpha" << "remove" << "-alpha" << "off";
    }

    int q = 85;
    if (options.quality == "high") q = 95;
    else if (options.quality == "compact") q = 70;
    args << "-quality" << QString::number(q);

    args << tempOutputPath;
    return args;
}

QStringList Converter::buildGhostscriptArgs(const QString &inputPath, const QString &tempOutputPath,
                                            const ConversionOptions &options) {
    QString preset = "/ebook";
    if (options.quality == "high") preset = "/printer";
    else if (options.quality == "compact") preset = "/screen";

    return {
        "-sDEVICE=pdfwrite",
        "-dCompatibilityLevel=1.4",
        "-dNOPAUSE",
        "-dQUIET",
        "-dBATCH",
        QString("-dPDFSETTINGS=%1").arg(preset),
        QString("-sOutputFile=%1").arg(tempOutputPath),
        inputPath
    };
}

bool Converter::atomicInstallFile(const QString &tempOrStagedPath, const QString &finalDestPath, QString *error) {
    const QFileInfo srcInfo(tempOrStagedPath);
    const QFileInfo dstInfo(finalDestPath);
    if (!srcInfo.exists() || srcInfo.size() <= 0) {
        if (error)
            *error = QObject::tr("Conversion produced an empty output file");
        return false;
    }

    QDir().mkpath(dstInfo.absolutePath());

    if (srcInfo.absolutePath() == dstInfo.absolutePath()) {
        if (QFile::rename(tempOrStagedPath, finalDestPath))
            return true;
        if (error)
            *error = QObject::tr("Failed to write output file %1").arg(finalDestPath);
        return false;
    }

    // Write beside destination first, then atomically rename so partial copies
    // across filesystems never leave a corrupt target file.
    const QString besideTemp = QDir(dstInfo.absolutePath()).filePath(
        QString(".morph-tmp-%1.%2").arg(QUuid::createUuid().toString(QUuid::Id128).left(8),
                                       dstInfo.suffix()));
    if (!QFile::copy(tempOrStagedPath, besideTemp)) {
        QFile::remove(besideTemp);
        if (error)
            *error = QObject::tr("Failed to copy output file into %1").arg(dstInfo.absolutePath());
        return false;
    }
    if (!QFile::rename(besideTemp, finalDestPath)) {
        QFile::remove(besideTemp);
        if (error)
            *error = QObject::tr("Failed to move output file into %1").arg(finalDestPath);
        return false;
    }
    QFile::remove(tempOrStagedPath);
    return true;
}

ConversionResult Converter::runSync(const QString &sourcePath, const QString &targetExt,
                                    const ConversionOptions &options, const ToolRegistry &tools) {
    ConversionResult res;
    const QFileInfo srcInfo(sourcePath);
    if (!srcInfo.exists() || !srcInfo.isFile()) {
        res.error = QObject::tr("Input file not found: %1").arg(sourcePath);
        return res;
    }

    const QString srcExt = sourceExtension(sourcePath);
    const QString dstExt = normalizeExt(targetExt);
    const QStringList allowed = targetFormatsForExtension(srcExt);
    if (allowed.isEmpty()) {
        res.error = QObject::tr("Unsupported source format: .%1").arg(srcExt);
        return res;
    }
    if (!allowed.contains(dstExt)) {
        res.error = QObject::tr("Cannot convert .%1 to .%2").arg(srcExt, dstExt);
        return res;
    }

    const QString tool = requiredTool(srcExt, dstExt);
    const QString exe = tools.find(tool);
    if (exe.isEmpty()) {
        res.error = QObject::tr("Required tool '%1' is not installed").arg(tool);
        return res;
    }

    const QString destDir = options.outputDirectory.isEmpty()
                                ? srcInfo.absolutePath()
                                : QDir(options.outputDirectory).absolutePath();
    if (!QDir().mkpath(destDir)) {
        res.error = QObject::tr("Could not create output folder: %1").arg(destDir);
        return res;
    }

    auto makeTempBesideTarget = [&](const QString &ext) {
        return QDir(destDir).filePath(QString(".morph-tmp-%1.%2")
                                          .arg(QUuid::createUuid().toString(QUuid::Id128).left(8), ext));
    };

    auto runTool = [](const QString &program, const QStringList &args, const QString &workDir, QString *errOut) {
        QProcess proc;
        if (!workDir.isEmpty())
            proc.setWorkingDirectory(workDir);
        proc.start(program, args);
        if (!proc.waitForStarted()) {
            *errOut = QObject::tr("Failed to start %1").arg(program);
            return false;
        }
        proc.waitForFinished(-1);
        if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
            const QString stderrText = QString::fromUtf8(proc.readAllStandardError()).trimmed();
            *errOut = stderrText.isEmpty()
                          ? QObject::tr("%1 exited with code %2").arg(QFileInfo(program).fileName()).arg(proc.exitCode())
                          : stderrText.section('\n', -4).trimmed();
            return false;
        }
        return true;
    };

    const QString cat = categoryForExtension(srcExt);

    if (cat == "video" || cat == "audio") {
        const QString tempOut = makeTempBesideTarget(dstExt);
        const QString finalOut = uniqueOutputPath(sourcePath, dstExt, options.outputDirectory);
        if (!runTool(exe, buildFfmpegArgs(sourcePath, tempOut, srcExt, dstExt, options), {}, &res.error)) {
            QFile::remove(tempOut);
            return res;
        }
        if (!atomicInstallFile(tempOut, finalOut, &res.error)) {
            QFile::remove(tempOut);
            return res;
        }
        res.ok = true;
        res.outputPath = finalOut;
        res.outputFiles = {finalOut};
        return res;
    }

    if (cat == "image") {
        const QString tempOut = makeTempBesideTarget(dstExt);
        const QString finalOut = uniqueOutputPath(sourcePath, dstExt, options.outputDirectory);
        if (!runTool(exe, buildMagickArgs(sourcePath, tempOut, srcExt, dstExt, options), {}, &res.error)) {
            QFile::remove(tempOut);
            return res;
        }
        if (!atomicInstallFile(tempOut, finalOut, &res.error)) {
            QFile::remove(tempOut);
            return res;
        }
        res.ok = true;
        res.outputPath = finalOut;
        res.outputFiles = {finalOut};
        return res;
    }

    if (cat == "document" || cat == "spreadsheet" || cat == "presentation") {
        QTemporaryDir stageDir;
        QTemporaryDir profileDir;
        if (!stageDir.isValid() || !profileDir.isValid()) {
            res.error = QObject::tr("Could not create temporary staging directory");
            return res;
        }
        const QStringList args{
            "--headless",
            "--norestore",
            QString("-env:UserInstallation=file://%1").arg(profileDir.path()),
            "--convert-to",
            libreOfficeFormatName(dstExt),
            "--outdir",
            stageDir.path(),
            sourcePath
        };
        if (!runTool(exe, args, {}, &res.error))
            return res;

        QDir stage(stageDir.path());
        const QStringList produced = stage.entryList({"*." + dstExt}, QDir::Files);
        if (produced.isEmpty()) {
            res.error = QObject::tr("LibreOffice did not produce a .%1 file").arg(dstExt);
            return res;
        }
        const QString finalOut = uniqueOutputPath(sourcePath, dstExt, options.outputDirectory);
        if (!atomicInstallFile(stage.filePath(produced.first()), finalOut, &res.error))
            return res;
        res.ok = true;
        res.outputPath = finalOut;
        res.outputFiles = {finalOut};
        return res;
    }

    if (cat == "pdf") {
        if (dstExt == "pdf") {
            const QString tempOut = makeTempBesideTarget("pdf");
            const QString finalOut = uniqueOutputPath(sourcePath, "pdf", options.outputDirectory, " (compressed)");
            if (!runTool(exe, buildGhostscriptArgs(sourcePath, tempOut, options), {}, &res.error)) {
                QFile::remove(tempOut);
                return res;
            }
            if (!atomicInstallFile(tempOut, finalOut, &res.error)) {
                QFile::remove(tempOut);
                return res;
            }
            res.ok = true;
            res.outputPath = finalOut;
            res.outputFiles = {finalOut};
            return res;
        }
        if (dstExt == "txt") {
            const QString tempOut = makeTempBesideTarget("txt");
            const QString finalOut = uniqueOutputPath(sourcePath, "txt", options.outputDirectory);
            if (!runTool(exe, {sourcePath, tempOut}, {}, &res.error)) {
                QFile::remove(tempOut);
                return res;
            }
            if (!atomicInstallFile(tempOut, finalOut, &res.error)) {
                QFile::remove(tempOut);
                return res;
            }
            res.ok = true;
            res.outputPath = finalOut;
            res.outputFiles = {finalOut};
            return res;
        }

        // PDF -> png / jpg / webp via pdftoppm (+ magick if webp)
        if (dstExt == "webp" && !tools.has("magick")) {
            res.error = QObject::tr("Required tool 'magick' is not installed");
            return res;
        }
        QTemporaryDir stageDir;
        if (!stageDir.isValid()) {
            res.error = QObject::tr("Could not create temporary staging directory");
            return res;
        }
        const int dpi = options.quality == "high" ? 200 : (options.quality == "compact" ? 100 : 150);
        const QString ppmFmt = (dstExt == "jpg") ? "-jpeg" : "-png";
        const QString ppmExt = (dstExt == "jpg") ? "jpg" : "png";
        const QStringList args{ppmFmt, "-r", QString::number(dpi), sourcePath, stageDir.filePath("page")};
        if (!runTool(exe, args, {}, &res.error))
            return res;

        QDir stage(stageDir.path());
        QStringList pages = stage.entryList({"page-*." + ppmExt}, QDir::Files, QDir::Name);
        if (pages.isEmpty()) {
            res.error = QObject::tr("No pages were rendered from PDF");
            return res;
        }

        if (pages.size() == 1) {
            QString srcPage = stage.filePath(pages.first());
            if (dstExt == "webp") {
                const QString webpStage = stage.filePath("single.webp");
                if (!runTool(tools.find("magick"), buildMagickArgs(srcPage, webpStage, "png", "webp", options), {}, &res.error))
                    return res;
                srcPage = webpStage;
            }
            const QString finalOut = uniqueOutputPath(sourcePath, dstExt, options.outputDirectory);
            if (!atomicInstallFile(srcPage, finalOut, &res.error))
                return res;
            res.ok = true;
            res.outputPath = finalOut;
            res.outputFiles = {finalOut};
            return res;
        }

        // Multi-page PDF export: preserve page order with zero-padded page numbers.
        QStringList installed;
        for (int i = 0; i < pages.size(); ++i) {
            QString srcPage = stage.filePath(pages.at(i));
            if (dstExt == "webp") {
                const QString webpStage = stage.filePath(QString("out-%1.webp").arg(i + 1, 3, 10, QChar('0')));
                if (!runTool(tools.find("magick"), buildMagickArgs(srcPage, webpStage, "png", "webp", options), {}, &res.error))
                    return res;
                srcPage = webpStage;
            }
            const QString suffix = QString("-page-%1").arg(i + 1, 2, 10, QChar('0'));
            const QString finalPage = uniqueOutputPath(sourcePath, dstExt, options.outputDirectory, suffix);
            if (!atomicInstallFile(srcPage, finalPage, &res.error))
                return res;
            installed.append(finalPage);
        }
        res.ok = true;
        res.outputPath = installed.first();
        res.outputFiles = installed;
        res.note = QObject::tr("%n pages", "", installed.size());
        return res;
    }

    if (cat == "archive") {
        QTemporaryDir stageDir;
        if (!stageDir.isValid()) {
            res.error = QObject::tr("Could not create temporary staging directory");
            return res;
        }

        const bool srcIsTar = srcExt.startsWith("tar") || srcExt == "tgz" || srcExt == "txz" || srcExt == "tbz2";
        if (srcIsTar && tools.has("tar")) {
            if (!runTool(tools.find("tar"), {"-xf", sourcePath, "-C", stageDir.path()}, {}, &res.error))
                return res;
        } else if (tools.has("7z")) {
            if (!runTool(tools.find("7z"), {"x", "-y", QString("-o%1").arg(stageDir.path()), sourcePath}, {}, &res.error))
                return res;
        } else {
            res.error = QObject::tr("Required tool '7z' is not installed");
            return res;
        }

        const QString tempOut = makeTempBesideTarget(dstExt);
        const QString finalOut = uniqueOutputPath(sourcePath, dstExt, options.outputDirectory);
        bool packed = false;
        if (isTarVariant(dstExt)) {
            packed = runTool(exe, {"-caf", tempOut, "-C", stageDir.path(), "."}, {}, &res.error);
        } else {
            packed = runTool(exe, {"a", "-y", tempOut, "."}, stageDir.path(), &res.error);
        }
        if (!packed) {
            QFile::remove(tempOut);
            return res;
        }
        if (!atomicInstallFile(tempOut, finalOut, &res.error)) {
            QFile::remove(tempOut);
            return res;
        }
        res.ok = true;
        res.outputPath = finalOut;
        res.outputFiles = {finalOut};
        return res;
    }

    res.error = QObject::tr("Unsupported conversion");
    return res;
}
