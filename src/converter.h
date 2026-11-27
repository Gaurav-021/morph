#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

struct ConversionOptions {
    QString outputDirectory;         // Empty means same directory as source file.
    QString quality = "balanced";    // "high", "balanced", "compact"
    QString scale = "original";      // "original", "large", "medium", "small"
};

struct ConversionResult {
    bool ok = false;
    QString outputPath;
    QStringList outputFiles;
    QString error;
    QString note;
};

struct ToolRegistry {
    QHash<QString, QString> paths;   // tool name -> executable path (empty if missing)

    static ToolRegistry detect();
    QString find(const QString &tool) const { return paths.value(tool); }
    bool has(const QString &tool) const { return !paths.value(tool).isEmpty(); }
    QStringList missingTools() const;
};

class Converter {
public:
    static QString sourceExtension(const QString &filePath);
    static QString baseStem(const QString &filePath);
    static QString categoryForExtension(const QString &ext);
    static QStringList targetFormatsForExtension(const QString &ext);
    static QString defaultTargetForExtension(const QString &ext);
    static QString requiredTool(const QString &sourceExt, const QString &targetExt);

    static QString uniqueOutputPath(const QString &sourcePath, const QString &targetExt,
                                    const QString &outputDirectory, const QString &suffixHint = {});

    // Synchronous headless conversion helper used by CLI and tests.
    static ConversionResult runSync(const QString &sourcePath, const QString &targetExt,
                                    const ConversionOptions &options, const ToolRegistry &tools);

    // Argument builders (exposed for unit tests and shared by async/sync runners).
    static QStringList buildFfmpegArgs(const QString &inputPath, const QString &tempOutputPath,
                                       const QString &sourceExt, const QString &targetExt,
                                       const ConversionOptions &options);
    static QStringList buildMagickArgs(const QString &inputPath, const QString &tempOutputPath,
                                       const QString &sourceExt, const QString &targetExt,
                                       const ConversionOptions &options);
    static QStringList buildGhostscriptArgs(const QString &inputPath, const QString &tempOutputPath,
                                            const ConversionOptions &options);
    static QStringList buildLibreOfficeArgs(const QString &inputPath, const QString &profileDir,
                                            const QString &outDir, const QString &sourceExt,
                                            const QString &targetExt);
    static bool atomicInstallFile(const QString &tempOrStagedPath, const QString &finalDestPath, QString *error);
};
