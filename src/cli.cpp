#include "cli.h"
#include "converter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

namespace Cli {

namespace {

void printJson(const QJsonObject &obj, bool toErr = false) {
    QTextStream out(toErr ? stderr : stdout);
    out << QJsonDocument(obj).toJson(QJsonDocument::Compact) << '\n';
}

void printHelp() {
    QTextStream out(stdout);
    out << "Usage:\n"
        << "  morph [open] [files...]                    Launch the Morph window\n"
        << "  morph check [--json]                       Check installed conversion tools\n"
        << "  morph formats [file] [--json]              List supported target formats\n"
        << "  morph convert <files...> --to <ext> [opts] Convert files without a display\n"
        << "  morph skill install                        Install the agent skill for Morph\n\n"
        << "Convert options:\n"
        << "  --to <ext>           Target extension (mp4, mp3, png, pdf, zip, ...)\n"
        << "  --out <dir>          Output folder (defaults to each source file's folder)\n"
        << "  --quality <level>    high | balanced (default) | compact\n"
        << "  --scale <size>       original (default) | large | medium | small\n"
        << "  --json               Machine-readable JSON output\n";
}

} // namespace

bool isHeadlessCommand(const QStringList &args) {
    if (args.size() <= 1)
        return false;
    const QString cmd = args.at(1);
    return cmd == "check" || cmd == "formats" || cmd == "convert" || cmd == "skill"
           || cmd == "--help" || cmd == "-h" || cmd == "--version" || cmd == "-v";
}

QStringList initialFilesForGui(const QStringList &args) {
    QStringList files;
    int start = 1;
    if (args.size() > 1 && args.at(1) == "open")
        start = 2;
    for (int i = start; i < args.size(); ++i) {
        if (!args.at(i).startsWith('-'))
            files.append(QFileInfo(args.at(i)).absoluteFilePath());
    }
    return files;
}

int run(const QStringList &args) {
    const QString cmd = args.value(1);
    const bool json = args.contains("--json");

    if (cmd == "--help" || cmd == "-h") {
        printHelp();
        return 0;
    }

    if (cmd == "--version" || cmd == "-v") {
        QTextStream(stdout) << "morph 0.1.0\n";
        return 0;
    }

    if (cmd == "check") {
        const ToolRegistry tools = ToolRegistry::detect();
        const QStringList missing = tools.missingTools();
        if (json) {
            QJsonObject toolMap;
            for (auto it = tools.paths.begin(); it != tools.paths.end(); ++it)
                toolMap.insert(it.key(), it.value());
            printJson({
                {"ok", missing.isEmpty()},
                {"tools", toolMap},
                {"missing", QJsonArray::fromStringList(missing)}
            });
        } else {
            QTextStream out(stdout);
            for (auto it = tools.paths.begin(); it != tools.paths.end(); ++it)
                out << it.key() << ": " << (it.value().isEmpty() ? "MISSING" : it.value()) << '\n';
        }
        return missing.isEmpty() ? 0 : 1;
    }

    if (cmd == "formats") {
        QString fileArg;
        for (int i = 2; i < args.size(); ++i) {
            if (!args.at(i).startsWith('-')) {
                fileArg = args.at(i);
                break;
            }
        }
        if (!fileArg.isEmpty()) {
            const QString ext = Converter::sourceExtension(fileArg);
            const QString cat = Converter::categoryForExtension(ext);
            const QStringList targets = Converter::targetFormatsForExtension(ext);
            if (targets.isEmpty()) {
                if (json)
                    printJson({{"ok", false}, {"error", "Unsupported file extension"}}, true);
                else
                    QTextStream(stderr) << "Unsupported file extension: " << ext << '\n';
                return 1;
            }
            if (json) {
                printJson({
                    {"ok", true},
                    {"sourceExtension", ext},
                    {"category", cat},
                    {"defaultTarget", Converter::defaultTargetForExtension(ext)},
                    {"targets", QJsonArray::fromStringList(targets)}
                });
            } else {
                QTextStream(stdout) << cat << " (." << ext << ") -> " << targets.join(", ") << '\n';
            }
            return 0;
        }

        const QJsonObject matrix{
            {"video", QJsonArray::fromStringList(Converter::targetFormatsForExtension("mp4"))},
            {"audio", QJsonArray::fromStringList(Converter::targetFormatsForExtension("mp3"))},
            {"image", QJsonArray::fromStringList(Converter::targetFormatsForExtension("png"))},
            {"document", QJsonArray::fromStringList(Converter::targetFormatsForExtension("docx"))},
            {"spreadsheet", QJsonArray::fromStringList(Converter::targetFormatsForExtension("xlsx"))},
            {"presentation", QJsonArray::fromStringList(Converter::targetFormatsForExtension("pptx"))},
            {"pdf", QJsonArray::fromStringList(Converter::targetFormatsForExtension("pdf"))},
            {"archive", QJsonArray::fromStringList(Converter::targetFormatsForExtension("zip"))}
        };
        if (json) {
            printJson({{"ok", true}, {"formats", matrix}});
        } else {
            QTextStream out(stdout);
            for (auto it = matrix.begin(); it != matrix.end(); ++it) {
                QStringList items;
                for (const auto &v : it.value().toArray())
                    items.append(v.toString());
                out << it.key() << ": " << items.join(", ") << '\n';
            }
        }
        return 0;
    }

    if (cmd == "skill" && args.value(2) == "install") {
        QFile bundled(":/SKILL.md");
        if (!bundled.open(QIODevice::ReadOnly)) {
            QTextStream(stderr) << "Bundled SKILL.md resource missing\n";
            return 1;
        }
        const QString targetDir = QDir::homePath() + "/.agents/skills/morph";
        if (!QDir().mkpath(targetDir)) {
            QTextStream(stderr) << "Could not create skill directory: " << targetDir << '\n';
            return 1;
        }
        const QString targetPath = targetDir + "/SKILL.md";
        QFile out(targetPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QTextStream(stderr) << "Could not write " << targetPath << '\n';
            return 1;
        }
        out.write(bundled.readAll());
        out.close();
        if (json)
            printJson({{"ok", true}, {"path", targetPath}});
        else
            QTextStream(stdout) << "Installed skill to " << targetPath << '\n';
        return 0;
    }

    if (cmd == "convert") {
        QString targetExt;
        ConversionOptions opts;
        QStringList inputs;

        for (int i = 2; i < args.size(); ++i) {
            const QString arg = args.at(i);
            if (arg == "--json") {
                continue;
            } else if (arg == "--to" && i + 1 < args.size()) {
                targetExt = args.at(++i);
            } else if (arg == "--out" && i + 1 < args.size()) {
                opts.outputDirectory = args.at(++i);
            } else if (arg == "--quality" && i + 1 < args.size()) {
                opts.quality = args.at(++i);
            } else if (arg == "--scale" && i + 1 < args.size()) {
                opts.scale = args.at(++i);
            } else if (arg.startsWith('-')) {
                if (json)
                    printJson({{"ok", false}, {"error", "Unknown option: " + arg}}, true);
                else
                    QTextStream(stderr) << "Unknown option: " << arg << '\n';
                return 1;
            } else {
                inputs.append(arg);
            }
        }

        if (inputs.isEmpty() || targetExt.isEmpty()) {
            const QString msg = "Usage: morph convert <files...> --to <ext> [--out <dir>] [--quality high|balanced|compact] [--scale original|large|medium|small] [--json]";
            if (json)
                printJson({{"ok", false}, {"error", msg}}, true);
            else
                QTextStream(stderr) << msg << '\n';
            return 1;
        }

        const ToolRegistry tools = ToolRegistry::detect();
        QJsonArray resultsJson;
        bool allOk = true;
        for (const QString &input : inputs) {
            const ConversionResult r = Converter::runSync(QFileInfo(input).absoluteFilePath(), targetExt, opts, tools);
            if (!r.ok)
                allOk = false;
            if (json) {
                resultsJson.append(QJsonObject{
                    {"input", input},
                    {"ok", r.ok},
                    {"output", r.outputPath},
                    {"outputs", QJsonArray::fromStringList(r.outputFiles)},
                    {"error", r.error}
                });
            } else if (r.ok) {
                QTextStream(stdout) << input << " -> " << r.outputPath << '\n';
            } else {
                QTextStream(stderr) << input << ": " << r.error << '\n';
            }
        }

        if (json)
            printJson({{"ok", allOk}, {"results", resultsJson}}, !allOk);
        return allOk ? 0 : 1;
    }

    QTextStream(stderr) << "Unknown command. Try morph --help\n";
    return 1;
}

} // namespace Cli
