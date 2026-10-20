# Note sulla CI (GitHub Actions)

## Cosa fa il workflow

`.github/workflows/build.yml` parte a ogni `push`, `pull_request` e a mano (`workflow_dispatch`). Tre job indipendenti (un errore su una piattaforma non nasconde le altre, non c'e' `needs`):

| Job | Runner | Preset | Cartella di build | Artifact |
| --- | --- | --- | --- | --- |
| `windows` | windows-2022 | `windows-ci-x64` | `build_x64` | `obs-multiview-pro-windows-x64` (dll + pdb) |
| `macos` | macos-15 (Xcode) | `macos-ci` | `build_macos` | `obs-multiview-pro-macos-universal` (zip del bundle `.plugin`) |
| `ubuntu` | ubuntu-24.04 | `ubuntu-ci-x86_64` | `build_x86_64` | `obs-multiview-pro-ubuntu-24.04-x86_64` (`.so`) |

Ogni job: checkout, configure col preset CI, build `RelWithDebInfo`, `ctest --output-on-failure -C RelWithDebInfo` (i test non richiedono OBS ne' Qt), upload del plugin. I preset CI attivano `CMAKE_COMPILE_WARNING_AS_ERROR`: qualunque warning del compilatore fa fallire la build.

- Windows e macOS: la configure scarica OBS 32.1.2, obs-deps e Qt6 (2025-08-23) secondo `buildspec.json` (hash verificati) e compila libobs/frontend-api da sorgente, come obs-plugintemplate. Il preset `windows-x64` nomina il generatore "Visual Studio 18 2026" (quello locale); il runner ha VS 2022, quindi il workflow passa `-G "Visual Studio 17 2022" -A x64`.
- Ubuntu: nessuna dipendenza scaricata da CMake; libobs/frontend-api vengono dal PPA `ppa:obsproject/obs-studio`, Qt6 da Ubuntu (stessi pacchetti di obs-plugintemplate).
- Permessi: solo `contents: read`. Nessun secret, nessuna firma, notarizzazione, installer o release (fuori dalla fase 1). Concurrency: un run nuovo sullo stesso ref annulla quello in corso. Le action usate sono `actions/checkout@v4` e `actions/upload-artifact@v4`.
- Il segnaposto di `qlalr.exe` (vedi `dev-environment-notes.md`) serve solo in locale: sui runner il vero `qlalr.exe` e' ok e non va replicato.

## Stato: mai eseguito

Il workflow non e' mai girato. Serve che il progetto sia in un repository GitHub (da creare e pubblicare a cura dell'utente); finche' non c'e', nulla lo esegue. Prima della consegna e' stato solo controllato a mano: struttura YAML, nomi dei preset (esistono tutti in `CMakePresets.json`), shell per runner (pwsh su Windows, bash altrove). La build Windows locale e' stata rifatta con `CMAKE_COMPILE_WARNING_AS_ERROR=ON` (stesso `-Werror` della CI): passa, `ctest` 3/3.

## Da controllare al primo run

1. Windows: la configure trova VS 2022 e un Windows SDK >= 10.0.20348; il download delle dipendenze (qlalr reale incluso) va a buon fine.
2. macOS: Xcode del runner >= 16.0 (richiesto da `cmake/macos/compilerconfig.cmake`); se `macos-15` ha un Xcode diverso, aggiungere un `sudo xcode-select --switch /Applications/Xcode_<versione>.app/Contents/Developer` prima della configure. Il bundle e' firmato solo ad-hoc.
3. Ubuntu: il PPA fornisce libobs 32.x (`find_package(libobs)`) e `OBS::obs-frontend-api`; Qt di Ubuntu 24.04 e' 6.4 (non 6.8).
4. Leggere i log di warning: con `-Werror` il primo errore su macOS/Linux e' probabilmente un warning clang/gcc mai visto (MSVC e' meno severo).
5. Scaricare gli artifact e verificare che il contenuto sia quello atteso (un solo `.dll`/`.plugin`/`.so`).

## Aree non verificate (mai compilate fuori da Windows)

- Blocchi di creazione display in `src/multiview_window.cpp`: macOS (`gs_window.view = (id)winId()`, Metal) e Linux X11 (`gs_window.id` + `obs_get_nix_platform_display()`, OpenGL). Wayland funziona solo con Qt >= 6.9; con Qt 6.4 del PPA il plugin scrive un avviso nel log e la finestra resta vuota (usare `QT_QPA_PLATFORM=xcb`).
- Font dei testi (`Helvetica` su macOS, `Monospace`/`Sans Serif` su Linux): le metriche delle etichette possono differire da Windows.
- Rendering con effetti/texture su Metal e OpenGL: solo Direct3D 11 e' stato provato.
- Warning clang (`-Wsign-compare`, `-Wcomma`, `-Wmove`, `-Wunused-parameter`...) e gcc (`-Wextra`) trattati come errori: verificati solo per lettura del codice.
