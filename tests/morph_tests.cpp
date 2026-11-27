#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "cli.h"
#include "converter.h"
#include "filepicker.h"
#include "theme.h"
#include "translator.h"

class FakeFilePicker : public FilePicker {
    Q_OBJECT
public:
    explicit FakeFilePicker(QObject *parent = nullptr) : FilePicker(parent) {}

    QStringList nextFiles;
    QString nextDirectory;

    void openFiles(const QString &, bool, const QString &) override {
        if (nextFiles.isEmpty())
            emit cancelled();
        else
            emit filesPicked(nextFiles);
    }

    void openDirectory(const QString &, const QString &) override {
        if (nextDirectory.isEmpty())
            emit cancelled();
        else
            emit directoryPicked(nextDirectory);
    }
};

class MorphTests : public QObject {
    Q_OBJECT

private slots:
    void detectsExtensionsAndCategories()
    {
        QCOMPARE(Converter::sourceExtension("/tmp/backup.tar.gz"), QString("tar.gz"));
        QCOMPARE(Converter::baseStem("/tmp/backup.tar.gz"), QString("backup"));
        QCOMPARE(Converter::sourceExtension("/tmp/clip.MOV"), QString("mov"));
        QCOMPARE(Converter::categoryForExtension("mov"), QString("video"));
        QCOMPARE(Converter::categoryForExtension("flac"), QString("audio"));
        QCOMPARE(Converter::categoryForExtension("webp"), QString("image"));
        QCOMPARE(Converter::categoryForExtension("docx"), QString("document"));
        QCOMPARE(Converter::categoryForExtension("xlsx"), QString("spreadsheet"));
        QCOMPARE(Converter::categoryForExtension("pptx"), QString("presentation"));
        QCOMPARE(Converter::categoryForExtension("pdf"), QString("pdf"));
        QCOMPARE(Converter::categoryForExtension("tar.gz"), QString("archive"));
        QCOMPARE(Converter::categoryForExtension("xyz"), QString("unknown"));
    }

    void picksDefaultTargetsAndRequiredTools()
    {
        QCOMPARE(Converter::defaultTargetForExtension("mov"), QString("mp4"));
        QCOMPARE(Converter::defaultTargetForExtension("mp4"), QString("mkv"));
        QCOMPARE(Converter::defaultTargetForExtension("png"), QString("jpg"));
        QCOMPARE(Converter::defaultTargetForExtension("pdf"), QString("png"));
        QCOMPARE(Converter::defaultTargetForExtension("zip"), QString("7z"));

        QCOMPARE(Converter::requiredTool("mp4", "mp3"), QString("ffmpeg"));
        QCOMPARE(Converter::requiredTool("png", "webp"), QString("magick"));
        QCOMPARE(Converter::requiredTool("docx", "pdf"), QString("libreoffice"));
        QCOMPARE(Converter::requiredTool("pdf", "png"), QString("pdftoppm"));
        QCOMPARE(Converter::requiredTool("pdf", "txt"), QString("pdftotext"));
        QCOMPARE(Converter::requiredTool("pdf", "pdf"), QString("gs"));
        QCOMPARE(Converter::requiredTool("zip", "tar.gz"), QString("tar"));
        QCOMPARE(Converter::requiredTool("tar.gz", "zip"), QString("7z"));
    }

    void uniqueOutputNeverOverwritesExistingOrSource()
    {
        QTemporaryDir dir;
        const QString src = dir.filePath("photo.png");
        write(src, "png");
        const QString first = Converter::uniqueOutputPath(src, "jpg", {});
        QCOMPARE(first, dir.filePath("photo.jpg"));

        write(first, "jpg");
        const QString second = Converter::uniqueOutputPath(src, "jpg", {});
        QCOMPARE(second, dir.filePath("photo (1).jpg"));

        const QString sameExt = Converter::uniqueOutputPath(src, "png", {});
        QCOMPARE(sameExt, dir.filePath("photo (1).png"));
    }

    void buildsExpectedToolArgs()
    {
        ConversionOptions opts;
        opts.quality = "high";
        opts.scale = "medium";

        const QStringList ffArgs = Converter::buildFfmpegArgs("/in.mov", "/out.mp4", "mov", "mp4", opts);
        QVERIFY(ffArgs.contains("libx264"));
        QVERIFY(ffArgs.contains("18"));
        QVERIFY(ffArgs.contains("scale=-2:'min(720,ih)'"));

        const QStringList imgArgs = Converter::buildMagickArgs("/in.svg", "/out.webp", "svg", "webp", opts);
        QVERIFY(imgArgs.contains("-background"));
        QVERIFY(imgArgs.contains("1280x1280>"));
        QVERIFY(imgArgs.contains("95"));

        const QStringList gsArgs = Converter::buildGhostscriptArgs("/in.pdf", "/out.pdf", opts);
        QVERIFY(gsArgs.contains("-dPDFSETTINGS=/printer"));
    }

    void backendQueueUndoRedoAndBatchFormat()
    {
        QTemporaryDir dir;
        const QString a = dir.filePath("a.png");
        const QString b = dir.filePath("b.tiff");
        const QString bad = dir.filePath("nope.unknown");
        write(a, "a");
        write(b, "b");
        write(bad, "bad");

        FakeFilePicker picker;
        picker.nextFiles = {a, b, bad};

        Backend backend(&picker, ToolRegistry::detect());
        backend.pickFiles();

        QCOMPARE(backend.count(), 2);
        QVERIFY(!backend.errorMessage().isEmpty());
        QCOMPARE(backend.selectedIndex(), 0);
        QVERIFY(backend.canUndo());

        backend.setTargetFormat(0, "webp");
        backend.applyFormatToCategory(0);
        auto *model = qobject_cast<QueueModel *>(backend.queue());
        QVERIFY(model != nullptr);
        QCOMPARE(model->jobAt(0).targetFormat, QString("webp"));
        QCOMPARE(model->jobAt(1).targetFormat, QString("webp"));

        backend.undo();
        QCOMPARE(model->jobAt(1).targetFormat, QString("png")); // before applyFormatToCategory
        backend.undo();
        QCOMPARE(model->jobAt(0).targetFormat, QString("jpg")); // before setTargetFormat

        backend.redo();
        QCOMPARE(model->jobAt(0).targetFormat, QString("webp"));

        picker.nextDirectory = dir.path();
        backend.pickOutputDirectory();
        QCOMPARE(backend.outputDirectory(), QDir(dir.path()).absolutePath());
    }

    void convertsRealImageAndArchiveEndToEnd()
    {
        const ToolRegistry tools = ToolRegistry::detect();
        if (!tools.has("magick") || !tools.has("7z") || !tools.has("tar"))
            QSKIP("magick / 7z / tar not installed");

        QTemporaryDir dir;
        const QString pngPath = dir.filePath("sample.png");
        QProcess mkImg;
        mkImg.start(tools.find("magick"), {"-size", "32x32", "xc:gold", pngPath});
        QVERIFY(mkImg.waitForFinished(5000));
        QCOMPARE(mkImg.exitCode(), 0);

        ConversionOptions opts;
        opts.quality = "balanced";
        const ConversionResult imgRes = Converter::runSync(pngPath, "webp", opts, tools);
        QVERIFY2(imgRes.ok, qPrintable(imgRes.error));
        QVERIFY(QFileInfo::exists(imgRes.outputPath));
        QVERIFY(QFileInfo(imgRes.outputPath).size() > 0);

        // Archive zip -> tar.gz
        const QString txtPath = dir.filePath("hello.txt");
        write(txtPath, "hello morph\n");
        const QString zipPath = dir.filePath("bundle.zip");
        QProcess mkZip;
        mkZip.setWorkingDirectory(dir.path());
        mkZip.start(tools.find("7z"), {"a", "-y", zipPath, "hello.txt"});
        QVERIFY(mkZip.waitForFinished(5000));
        QCOMPARE(mkZip.exitCode(), 0);

        const ConversionResult arcRes = Converter::runSync(zipPath, "tar.gz", opts, tools);
        QVERIFY2(arcRes.ok, qPrintable(arcRes.error));
        QVERIFY(arcRes.outputPath.endsWith(".tar.gz"));
        QVERIFY(QFileInfo::exists(arcRes.outputPath));
        QVERIFY(QFileInfo(arcRes.outputPath).size() > 0);
    }

    void cliHeadlessCommandsWork()
    {
        QCOMPARE(Cli::run({"morph", "check", "--json"}), 0);
        QCOMPARE(Cli::run({"morph", "formats", "clip.mp4", "--json"}), 0);
        QCOMPARE(Cli::run({"morph", "formats", "bad.xyz", "--json"}), 1);
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        write(response, "{\"int\":0}");

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        write(response, "{\"int\":6}");
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Add files").isNull());
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Add files": "Adicionar ficheiros", "Cancel": "Cancelar"})");
        write(dir.path() + "/pt-BR.json", R"({"Add files": "Adicionar arquivos"})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Add files"), QString("Adicionar arquivos"));
        QCOMPARE(translator.translate("Main", "Cancel"), QString("Cancelar"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n files": {"one": "%n file", "other": "%n files"}})");
        write(dir.path() + "/ru.json", R"({"%n files": {"one": "%n файл", "few": "%n файла", "many": "%n файлов", "other": "%n файла"}})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n files", nullptr, 21), QString("%n файл"));
        QCOMPARE(translator.translate("Main", "%n files", nullptr, 22), QString("%n файла"));
        QCOMPARE(translator.translate("Main", "%n files", nullptr, 25), QString("%n файлов"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }

private:
    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(contents), contents.size());
    }
};

QTEST_MAIN(MorphTests)
#include "morph_tests.moc"
