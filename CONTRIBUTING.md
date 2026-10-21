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

By contributing you agree that your work is released under GPL-2.0-or-later.
