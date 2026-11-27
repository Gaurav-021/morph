---
name: morph
description: Convert videos, audio, images, documents, PDFs, and archives locally via the morph CLI.
---

# Morph CLI

Use `morph` to convert files locally without opening a display window.

## Commands

- Check installed tools: `morph check --json`
- List supported target formats (all or for a specific file): `morph formats [file] --json`
- Convert files headlessly:
  ```sh
  morph convert input.mov --to mp4 --quality balanced --scale original --json
  morph convert *.png --to webp --out ./webp --quality high --json
  ```
- Open files in the GUI: `morph open file1 file2`

Options for `morph convert`:
- `--to <ext>`: target extension (`mp4`, `mkv`, `webm`, `mp3`, `png`, `jpg`, `webp`, `pdf`, `zip`, `7z`, `tar.gz`, ...)
- `--out <dir>`: output directory (defaults to each input file's directory; never overwrites originals)
- `--quality <high|balanced|compact>`: compression/encoding quality preset
- `--scale <original|large|medium|small>`: optional resolution/dimension limit
- `--json`: machine-readable JSON output; exits 0 on success, 1 on failure
