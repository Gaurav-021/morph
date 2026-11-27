#pragma once

#include <QStringList>

namespace Cli {

// Returns true when arguments represent a headless CLI command (check, formats,
// convert, skill, --help, --version). Returns false when Morph should open the
// GUI (`morph`, `morph open ...`, or `morph <files...>`).
bool isHeadlessCommand(const QStringList &args);

// Runs the headless command and returns a process exit code (0 or 1).
int run(const QStringList &args);

// Extracts initial file paths when launching the GUI (`morph open f1 f2` or `morph f1 f2`).
QStringList initialFilesForGui(const QStringList &args);

} // namespace Cli
