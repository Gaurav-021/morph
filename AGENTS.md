# AGENTS.md

Guidelines for coding agents working on **Morph**.

## Build, test, and install

- `./bin/build` — compiles `build/morph` with `qmake6` and `make`
- `./bin/test` — builds and runs `tests/morph_tests` offscreen (`QT_QPA_PLATFORM=offscreen`)
- `./bin/install` — runs `makepkg -fsi` from `pkgbuild/` for local testing

Always run `./bin/build` and `./bin/test` after code changes.

## Project layout and architecture

- Keep `src/` flat and build with `qmake6` (`morph.pro`). Do not switch to CMake or commit `build/` artifacts.
- **C++ owns state, processes, and file I/O:**
  - `src/converter.{h,cpp}` — format matrix, argument builders, atomic file installation (`atomicInstallFile`), and headless `Converter::runSync`.
  - `src/backend.{h,cpp}` — `QueueModel` and async `Backend` (`QProcess`) exposed to QML as `backend`.
  - `src/cli.{h,cpp}` — headless commands (`check`, `formats`, `convert`, `skill install`) running with `QT_QPA_PLATFORM=offscreen`.
  - `src/filepicker.h` and `src/portalfilepicker.{h,cpp}` — `org.freedesktop.portal.FileChooser` over D-Bus. Never use `QFileDialog`.
  - `src/theme.{h,cpp}` — live watcher for `~/.local/state/omarchy/current/theme/colors.toml` and `hyprctl` `decoration:rounding`.
  - `src/translator.{h,cpp}` — live watcher for `~/.config/omarchy/language` and compiled JSON catalogs in `src/i18n/`.
- **QML owns layout and shortcuts:**
  - `src/Main.qml`, `src/CompactCombo.qml`, `src/ShortcutOverlay.qml`, compiled via `src/resources.qrc`.
  - Use `QGuiApplication`, Material dark style, `theme.accent`, `theme.radius`, and `LayoutMirroring` for RTL locales.

## Rules when adding formats or UI strings

- Never modify or overwrite a user's source file or an existing output file (`Converter::uniqueOutputPath`). Write to temp files beside the target and rename atomically on success.
- Wrap every user-visible string in `qsTr()` (QML) or `tr()` (C++), and keep all 19 catalogs in `src/i18n/` complete (`MorphTests::allI18nCatalogsAreValidAndComplete` checks them against `da.json`). Update `pkgbuild/morph.desktop` if adding new languages.
- Add or update unit/integration tests in `tests/morph_tests.cpp` for new conversion paths or backend behavior.
- If CLI options or output formats change, update `src/SKILL.md` (installed via `morph skill install`) and `README.md`.

## Releasing a new version

1. Update `app.setApplicationVersion(...)` in `src/main.cpp`, `"morph <version>"` in `src/cli.cpp`, and `pkgver` in `pkgbuild/PKGBUILD`.
2. Verify `./bin/build` and `./bin/test` pass cleanly.
3. Commit on `master`, tag `v<version>`, push `master` and the tag, and create the GitHub release on `Gaurav-021/morph`. Do not move or rewrite existing release tags (`omarchy-pkgs` tracks new `v<version>` releases automatically).
