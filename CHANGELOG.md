# Changelog

## Unreleased

First public alpha.

- Own multiview window with native look: program and preview, scene tiles drawn every frame, labels, safe areas.
- Presets: 4, 8, 16, 24, 32 scenes, and the same grids with four status tiles.
- Click, double-click, drag to swap, context menu, click a tile name to choose its scene.
- Stereo audio meters with dB scale on program and preview; mixer tile with the global audio devices.
- ON AIR, REC and clock tiles with times, bitrate, dropped frames, connection state, service and recording file.
- Settings saved per window and per scene collection.
- Settings dialog (Tools > Multiview Settings..., or right-click > Settings...): border colour, label opacity and meter opacity, shared by all windows, with live preview. Label opacity 0 hides the labels and the click-on-name menu; meter opacity fades each element, so lit and unlit contrast drops at low values.
- Update check (Settings > Updates): installed version, "Check for updates now", automatic check once a day (on by default; 60 s after OBS has loaded at the earliest, never while streaming or recording), optional pre-releases, change log of every newer release, "Open download page" (browser) and "Skip this version". Automatic results only add a line to the OBS log and an "Update available" entry to the window menu and the Tools menu. Nothing is downloaded or installed.
- One HTTPS request to api.github.com per check (User-Agent obs-multiview-pro/<version>, nothing else); see SECURITY.md.
- Release workflow: version tags build all three platforms and publish zips (Windows, macOS, Ubuntu) with SHA256SUMS.txt.
- Known limit: the Windows build of OBS 32 has no Qt TLS plugin, so the update check reports that secure connections are not available there.
- Tested on Windows 11 with OBS 32.2. macOS and Linux builds are produced by CI and still untested.
