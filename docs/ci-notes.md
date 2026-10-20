# Note sulla CI (GitHub Actions)

## Cosa fa il workflow

`.github/workflows/build.yml` parte a ogni `push` su un branch, `pull_request`, a mano (`workflow_dispatch`) e quando lo richiama `release.yml` (`workflow_call`). Tre job indipendenti (un errore su una piattaforma non nasconde le altre, non c'e' `needs`):

| Job | Runner | Preset | Cartella di build | Artifact |
| --- | --- | --- | --- | --- |
| `windows` | windows-2022 | `windows-ci-x64` | `build_x64` | `obs-multiview-pro-windows-x64` (dll + pdb) |
| `macos` | macos-15 (Xcode) | `macos-ci` | `build_macos` | `obs-multiview-pro-macos-universal` (zip del bundle `.plugin`) |
| `ubuntu` | ubuntu-24.04 | `ubuntu-ci-x86_64` | `build_x86_64` | `obs-multiview-pro-ubuntu-24.04-x86_64` (`.so`) |

Ogni job: checkout, configure col preset CI, build `RelWithDebInfo`, `ctest --output-on-failure -C RelWithDebInfo` (i test non richiedono OBS ne' Qt), upload del plugin. I preset CI attivano `CMAKE_COMPILE_WARNING_AS_ERROR`: qualunque warning del compilatore fa fallire la build.

- Windows e macOS: la configure scarica OBS 32.1.2, obs-deps e Qt6 (2025-08-23) secondo `buildspec.json` (hash verificati) e compila libobs/frontend-api da sorgente, come obs-plugintemplate. Il preset `windows-x64` nomina il generatore "Visual Studio 18 2026" (quello locale); il runner ha VS 2022, quindi il workflow passa `-G "Visual Studio 17 2022" -A x64`.
- Ubuntu: nessuna dipendenza scaricata da CMake; libobs/frontend-api vengono dal PPA `ppa:obsproject/obs-studio`, Qt6 da Ubuntu (stessi pacchetti di obs-plugintemplate).
- Permessi: solo `contents: read`. Nessun secret, nessuna firma, notarizzazione o installer. Le release le fa `release.yml` (sotto). Concurrency: un run nuovo sullo stesso ref annulla quello in corso. Le action usate sono `actions/checkout@v4` e `actions/upload-artifact@v4`.
- libcurl (verifica aggiornamenti): `find_package(CURL REQUIRED)` -> `CURL::libcurl`. Windows: obs-deps (`.deps/obs-deps-*/lib/cmake/CURL`, libcurl 8.12.1 con Schannel; a runtime si usa la `libcurl.dll` che OBS installa accanto a `obs64.exe`, lo zip non ne contiene una). macOS: libcurl dell'SDK (come il frontend di OBS). Ubuntu: `libcurl4-openssl-dev` nella lista apt di `build.yml` (anche per le release, che riusano `build.yml`); a runtime `libcurl4`, dipendenza di OBS.
- Il segnaposto di `qlalr.exe` (vedi `dev-environment-notes.md`) serve solo in locale: sui runner il vero `qlalr.exe` e' ok e non va replicato.

## Release (`.github/workflows/release.yml`)

Parte solo al push di un tag `v*.*.*`; per ora il tag deve essere numerico, `vMAJOR.MINOR.PATCH` (es. `v0.2.0`): `cmake/common/bootstrap.cmake` passa `version` di `buildspec.json` a `project(VERSION ...)`, che accetta solo numeri, quindi una pre-release (`0.2.0-rc.1`) non si puo' costruire e il job `check` rifiuta un tag non numerico. `build.yml` ora parte per i push sui branch (non sui tag), per le pull request, a mano e come workflow riusabile (`workflow_call`): il workflow di release lo richiama, quindi build, test e artifact sono identici e un tag compila una volta sola.

| Job | Cosa fa | Permessi |
| --- | --- | --- |
| `check` | il tag deve essere `vMAJOR.MINOR.PATCH`; senza `v` deve essere uguale a `version` di `buildspec.json` (letto con `jq`), e `CHANGELOG.md` deve avere una sezione `## <versione>` non vuota (`.github/scripts/changelog-section.sh`); altrimenti fallisce subito con un messaggio chiaro | `contents: read` |
| `build` | `uses: ./.github/workflows/build.yml` (i tre job Windows / macOS / Ubuntu con ctest e upload) | `contents: read` |
| `release` | scarica gli artifact, crea gli zip, `SHA256SUMS.txt`, estrae le note dalla sezione del CHANGELOG e chiama `gh release create "$GITHUB_REF_NAME" --verify-tag --title ... --notes-file ...` | `contents: write`, solo qui |

Asset (layout pensato per estrarre lo zip direttamente nella cartella dei plugin):

- `obs-multiview-pro-<versione>-windows-x64.zip`: `obs-multiview-pro/bin/64bit/obs-multiview-pro.dll` + `.pdb` (in `C:\ProgramData\obs-studio\plugins\`).
- `obs-multiview-pro-<versione>-macos-universal.zip`: il bundle `obs-multiview-pro.plugin` zippato con `ditto` (artifact di `build.yml`, rinominato).
- `obs-multiview-pro-<versione>-ubuntu-24.04-x86_64.zip`: `obs-multiview-pro/bin/64bit/obs-multiview-pro.so`.
- `SHA256SUMS.txt`: `sha256sum` dei tre zip.

Nessuna action di terze parti: `actions/checkout@v4`, `actions/upload-artifact@v4`, `actions/download-artifact@v4` e `gh` (preinstallato sui runner, token `github.token`).

Per pubblicare: rinominare `## Unreleased` in `## <versione>` nel CHANGELOG, portare `version` di `buildspec.json` alla stessa versione, commit, poi `git tag v<versione>` e push del tag.

Verifiche fatte senza eseguirlo: struttura YAML con un controllo in Python (nessun PyYAML: parser del sottoinsieme usato; job, `needs`, `uses` versionati, `workflow_call` presente), estrazione delle note provata in locale su un CHANGELOG di prova (sezione presente, `v`/`[...]`/testo dopo la versione, sezione mancante o vuota -> errore), passi di packaging eseguiti in locale con artifact finti e `gh` simulato (nomi e contenuto degli zip, checksum). Percorsi controllati contro `CMakePresets.json` e gli upload di `build.yml` (`build_x64/rundir/RelWithDebInfo`, `build_macos/rundir/RelWithDebInfo/*.plugin`, `build_x86_64/rundir/RelWithDebInfo/*.so`).

Da controllare al primo tag:

1. Il job `check` legge la versione (jq) e trova la sezione del CHANGELOG.
2. Il workflow riusabile parte con i permessi `contents: read` e i tre job passano come in `build.yml`.
3. Gli artifact scaricati stanno in `artifacts/<nome-artifact>/` con i file attesi (un `.dll` + `.pdb`, un `.plugin.zip`, un `.so`); il job elenca i file e il contenuto degli zip nel log.
4. `gh release create` crea la release con i 4 asset.
5. Ubuntu: `libcurl4-openssl-dev` installato da apt e trovato da `find_package(CURL)`; macOS: `find_package(CURL)` trova la libcurl dell'SDK (come fa OBS stesso) o quella di obs-deps se presente, universale arm64+x86_64.

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
