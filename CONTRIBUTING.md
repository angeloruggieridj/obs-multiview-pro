# Contributing

Thanks for helping. The plugin has only been run on Windows 11 so far, so bug reports from macOS and Linux are especially useful.

## Reporting a bug

Open an issue with: OBS Studio version, operating system, the multiview preset you use, what you did, what you expected, and what happened. Attach the OBS log (Help → Log Files → Upload Current Log File) if OBS crashed or the window stayed empty.

## Pull requests

1. Open an issue first for anything bigger than a small fix.
2. Build and run the tests (see the README). New pure logic gets a unit test in `tests/`.
3. Keep the draw path allocation-free and the audio callbacks atomic-only. The plugin must never block the OBS UI, graphics or audio threads.
4. Use public OBS APIs only (libobs, obs-frontend-api, Qt).
5. Commit messages: short imperative subject with a module prefix, for example `render: fix label clipping`.

## Translations

Every text the plugin shows lives in `data/locale/`; none is written in the C++ sources. `en-US.ini` is the source of truth, and OBS picks the file that matches its own language setting (*Settings → General → Language*), falling back to English for anything a translation lacks.

To add a language:

1. Copy `data/locale/en-US.ini` to `data/locale/<lang>.ini`, using the code OBS uses for that language (`de-DE`, `fr-FR`, `pt-BR`...).
2. Translate the values only. Keep every key, keep `%1` / `%2` (filled in by the plugin) and keep `\n` line breaks. Write `Key="Value"` on one line, `\"` for a quote, save as UTF-8 without BOM.
3. Run the tests (`ctest`): they fail if a key is missing, unknown or empty, or if a placeholder differs from the English text.

Tile captions (`Tile.*`) must stay short: they are drawn inside small tiles.

To add a text in code, add its key to `en-US.ini` (and `it-IT.ini`) and read it with `T("Key")` from `src/i18n.hpp`. The tests also reject user-visible string literals left in `src/`.

By contributing you agree that your work is released under GPL-2.0-or-later.
