# Changelog

## 1.0.0 - 2026-10-21

First stable release.

- A multiview window of its own with the look of the native one: program and preview, scene tiles drawn every frame, sharp labels, safe areas.
- Presets: 4, 8, 16, 24 and 32 scenes, and the same grids with four status tiles.
- Click to switch, double-click for a transition in studio mode, drag to swap, context menu, click a tile name to choose its scene.
- Stereo audio meters with dB scale on program and preview; a mixer tile with the global audio devices.
- ON AIR, REC and clock tiles: times, bitrate, dropped frames, connection state, service and recording file.
- Settings dialog (Tools > Multiview Settings..., or right-click > Settings...): border colour, label opacity and meter opacity, with live preview. Label opacity 0 hides the labels and the click-on-name menu.
- Update check (Settings > Updates): manual or automatic (once a day, on by default, never while streaming or recording), with the change log of every newer release. Nothing is downloaded or installed; the download page opens in your browser. One HTTPS request to api.github.com per check, with certificate verification on. See SECURITY.md.
- Layout, options and tile assignments are saved per window and per scene collection.
- Runtime-tested on Windows 11 with OBS 32.2. The macOS and Linux builds are compiled and unit-tested on CI but have not been run yet.
