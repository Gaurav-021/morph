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

    void convertsVideoAndAudioEndToEnd()
    {
        const ToolRegistry tools = ToolRegistry::detect();
        if (!tools.has("ffmpeg"))
            QSKIP("ffmpeg not installed");

        QTemporaryDir dir;
        const QString movPath = dir.filePath("clip.mov");
        QProcess mkMov;
        mkMov.start(tools.find("ffmpeg"), {
            "-hide_banner", "-y",
            "-f", "lavfi", "-i", "testsrc=duration=1:size=160x120:rate=10",
            "-f", "lavfi", "-i", "sine=frequency=440:duration=1",
            "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac",
            movPath
        });
        QVERIFY(mkMov.waitForFinished(15000));
        QCOMPARE(mkMov.exitCode(), 0);

        ConversionOptions opts;
        opts.quality = "compact";

        const ConversionResult mp4Res = Converter::runSync(movPath, "mp4", opts, tools);
        QVERIFY2(mp4Res.ok, qPrintable(mp4Res.error));
        QVERIFY(QFileInfo::exists(mp4Res.outputPath));
        QVERIFY(QFileInfo(mp4Res.outputPath).size() > 0);

        const ConversionResult gifRes = Converter::runSync(movPath, "gif", opts, tools);
        QVERIFY2(gifRes.ok, qPrintable(gifRes.error));
        QVERIFY(QFileInfo::exists(gifRes.outputPath));
        QVERIFY(QFileInfo(gifRes.outputPath).size() > 0);

        const ConversionResult mp3Res = Converter::runSync(movPath, "mp3", opts, tools);
        QVERIFY2(mp3Res.ok, qPrintable(mp3Res.error));
        QVERIFY(QFileInfo::exists(mp3Res.outputPath));
        QVERIFY(QFileInfo(mp3Res.outputPath).size() > 0);

        // Also test async Backend queue conversion with progress/completion.
        Backend backend(nullptr, tools);
        backend.addFiles({mp3Res.outputPath});
        backend.setTargetFormat(0, "wav");
        backend.convertAll();
        QVERIFY(backend.busy());
        QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 15000);
        QCOMPARE(backend.completedCount(), 1);
        QCOMPARE(backend.failedCount(), 0);
        auto *model = qobject_cast<QueueModel *>(backend.queue());
        QVERIFY(model != nullptr);
        QCOMPARE(model->jobAt(0).status, QString("done"));
        QVERIFY(QFileInfo::exists(model->jobAt(0).outputPath));
        QVERIFY(QFileInfo(model->jobAt(0).outputPath).size() > 0);
    }

    void convertsDocumentAndPdfEndToEnd()
    {
        const ToolRegistry tools = ToolRegistry::detect();
        if (!tools.has("libreoffice") || !tools.has("pdftoppm") || !tools.has("pdftotext") || !tools.has("gs"))
            QSKIP("libreoffice / poppler / gs not installed");

        QTemporaryDir dir;
        const QString htmlPath = dir.filePath("note.html");
        write(htmlPath,
              "<html><body>"
              "<div>Page 1 Morph Document</div>"
              "<div style=\"page-break-before: always;\">Page 2 Morph Document</div>"
              "</body></html>\n");

        ConversionOptions opts;
        opts.quality = "balanced";

        // Document (HTML) -> DOCX via LibreOffice
        const ConversionResult docxRes = Converter::runSync(htmlPath, "docx", opts, tools);
        QVERIFY2(docxRes.ok, qPrintable(docxRes.error));
        QVERIFY(QFileInfo::exists(docxRes.outputPath));
        QVERIFY(QFileInfo(docxRes.outputPath).size() > 0);

        // Document (HTML) -> multi-page PDF via LibreOffice
        const ConversionResult pdfRes = Converter::runSync(htmlPath, "pdf", opts, tools);
        QVERIFY2(pdfRes.ok, qPrintable(pdfRes.error));
        QVERIFY(QFileInfo::exists(pdfRes.outputPath));
        QVERIFY(QFileInfo(pdfRes.outputPath).size() > 0);

        // PDF -> compressed PDF via Ghostscript
        const ConversionResult compRes = Converter::runSync(pdfRes.outputPath, "pdf", opts, tools);
        QVERIFY2(compRes.ok, qPrintable(compRes.error));
        QVERIFY(QFileInfo::exists(compRes.outputPath));
        QVERIFY(QFileInfo(compRes.outputPath).size() > 0);

        // PDF -> TXT via pdftotext
        const ConversionResult txtRes = Converter::runSync(pdfRes.outputPath, "txt", opts, tools);
        QVERIFY2(txtRes.ok, qPrintable(txtRes.error));
        QFile txtFile(txtRes.outputPath);
        QVERIFY(txtFile.open(QIODevice::ReadOnly));
        const QString extracted = QString::fromUtf8(txtFile.readAll());
        QVERIFY(extracted.contains("Page 1 Morph Document"));
        QVERIFY(extracted.contains("Page 2 Morph Document"));

        // Single-page PDF -> PNG via pdftoppm
        const ConversionResult singlePngRes = Converter::runSync(pdfRes.outputPath, "png", opts, tools);
        QVERIFY2(singlePngRes.ok, qPrintable(singlePngRes.error));
        QCOMPARE(singlePngRes.outputFiles.size(), 1);
        QVERIFY(QFileInfo::exists(singlePngRes.outputPath));
        QVERIFY(QFileInfo(singlePngRes.outputPath).size() > 0);

        // Multi-page PDF -> PNG and WebP via pdftoppm (+ magick)
        if (tools.has("magick")) {
            const QString multiPdf = dir.filePath("multi.pdf");
            QProcess mkMulti;
            mkMulti.start(tools.find("magick"), {"-size", "64x64", "xc:tomato", "xc:steelblue", multiPdf});
            QVERIFY(mkMulti.waitForFinished(5000));
            QCOMPARE(mkMulti.exitCode(), 0);

            const ConversionResult pngRes = Converter::runSync(multiPdf, "png", opts, tools);
            QVERIFY2(pngRes.ok, qPrintable(pngRes.error));
            QCOMPARE(pngRes.outputFiles.size(), 2);
            for (const QString &pagePath : pngRes.outputFiles) {
                QVERIFY(QFileInfo::exists(pagePath));
                QVERIFY(QFileInfo(pagePath).size() > 0);
            }

            const ConversionResult webpRes = Converter::runSync(multiPdf, "webp", opts, tools);
            QVERIFY2(webpRes.ok, qPrintable(webpRes.error));
            QCOMPARE(webpRes.outputFiles.size(), 2);
            for (const QString &pagePath : webpRes.outputFiles) {
                QVERIFY(QFileInfo::exists(pagePath));
                QVERIFY(QFileInfo(pagePath).size() > 0);
            }
        }
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
