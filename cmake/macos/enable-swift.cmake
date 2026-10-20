# Injected into the OBS Studio sources configure step (CMAKE_PROJECT_obs-studio_INCLUDE).
# OBS 32.0 and 32.1 enable Swift only inside the mac-virtualcam plugin, which is switched
# off when only libobs is built (ENABLE_PLUGINS=OFF), but libobs-metal is written in Swift.
# OBS 32.2 and later enable Swift themselves, so this file can be dropped after a version bump.
enable_language(Swift)
