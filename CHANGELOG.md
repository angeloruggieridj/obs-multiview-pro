# Changelog

## Unreleased

- Runs on OBS Studio 33.0.0 (runtime-tested on Windows 11, Direct3D 11); still needs OBS 32.1 or newer.
- Windows: two zips with the same plugin, one for OBS 33 or newer (the new plugin folder layout) and one for OBS 32.1 and 32.2 (`bin\64bit`). OBS 33 flags the old layout as legacy.

## 1.1.0

- Audio meters on every scene tile: thin L/R bars with the loudest audio source of the scene (Settings and the window menu, on by default).
- One volmeter per audio source, shared by the scene tiles, the preview meter and the mixer; at most 128 per window, the others noted in the OBS log.
- Mixer tile lists the audio sources of the program scene (and of the preview scene in studio mode) next to the global devices; at most 7 sources beside PGM, the others noted in the OBS log.
- Mixer columns keep their place and peak hold across transitions.
- Option to keep the multiview windows always on top (Settings and the window menu); the settings dialog stays above them.
- Fixed: switching, creating or importing a scene collection with a multiview open could make OBS report a source leak and close.

## 1.0.0

First stable release.

- Own multiview window with the native look; presets of 4 to 32 scenes, with or without status tiles.
- Stereo audio meters on program and preview; mixer tile with the global audio devices.
- ON AIR, REC and clock tiles.
- Click, double-click, drag to swap, context menu; layout, options and tile assignments saved per window and scene collection.
- Settings: border colour, label opacity, meter opacity.
- Update check with change log: manual or once a day, never while streaming or recording. Nothing is downloaded or installed.
- English and Italian interface, following the OBS language.
- Runtime-tested on Windows 11 with OBS 32.2. macOS and Linux build and pass the unit tests on CI but have not been run.
