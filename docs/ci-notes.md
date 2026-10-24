# Note sulla CI (GitHub Actions)

## Cosa fa il workflow

`.github/workflows/build.yml` parte a ogni `push` su un branch, `pull_request`, a mano (`workflow_dispatch`) e quando lo richiama `release.yml` (`workflow_call`). Tre job indipendenti (un errore su una piattaforma non nasconde le altre, non c'e' `needs`):

| Job | Runner | Preset | Cartella di build | Artifact |
| --- | --- | --- | --- | --- |
| `windows` | windows-2022 | `windows-ci-x64` | `build_x64` | `obs-multiview-pro-windows-x64` (dll + pdb + `obs-multiview-pro/locale/*.ini`) |
| `macos` | macos-15 (Xcode) | `macos-ci` | `build_macos` | `obs-multiview-pro-macos-universal` (zip del bundle `.plugin`, con `Contents/Resources/locale/*.ini`) |
| `ubuntu` | ubuntu-24.04 | `ubuntu-ci-x86_64` | `build_x86_64` | `obs-multiview-pro-ubuntu-24.04-x86_64` (`.so` + `obs-multiview-pro/locale/*.ini`) |
| `ubuntu-26` | ubuntu-26.04 | `ubuntu-ci-x86_64` | `build_x86_64` | `obs-multiview-pro-ubuntu-26.04-x86_64` (usato dalla release per i due zip Ubuntu 26.04) |

`ubuntu-26`: OBS 33 non supporta piu' Ubuntu 24.04 e il suo PPA punta a 26.04 (Qt >= 6.10, quindi il ramo Wayland di `multiview_window.cpp` viene compilato). Primo run verde (24/10/2026): libobs 32.2.0 dal PPA, Qt 6.10.2, `-Werror` e 3 test su 3. Sui runner 26.04 apt chiede gli indici `amd64v3`, che il PPA lascia vuoti, e offre solo l'obs-studio di Ubuntu (32.1.0): per questo il job scarica il `.deb` dall'indice `amd64` del PPA (HTTPS, SHA256 controllato, ma senza verifica della firma GPG) e lo installa con apt. Il job fallisce se la libobs del PPA diventa la 33: una DLL/`.so` compilata contro libobs 33 viene rifiutata da OBS 32 (i moduli compilati con una libobs piu' nuova sono scartati), quindi `buildspec.json` resta su OBS 32.1.2 finche' si supporta OBS 32. Cosi' la stessa DLL Windows e' stata provata su OBS 33.0.0. Il `.so` per 26.04 non e' mai stato avviato in OBS.

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
| `release` | scarica gli artifact, crea gli zip, `SHA256SUMS.txt`, estrae le note dalla sezione del CHANGELOG (righe CRLF ripulite, i `## ` dentro i blocchi di codice non contano) e pubblica con `.github/scripts/publish-release.sh`: se esiste gia' una release pubblicata per il tag fallisce; altrimenti crea una bozza (`gh release create --draft --verify-tag`) o riusa quella lasciata da un run fallito, carica gli asset (`gh release upload --clobber`), controlla che i nomi siano esattamente quelli attesi e solo allora pubblica (`gh release edit --draft=false`). Le bozze non sono mai proposte dalla verifica aggiornamenti | `contents: write`, solo qui |

Asset (layout pensato per estrarre lo zip direttamente nella cartella dei plugin):

- `obs-multiview-pro-<versione>-windows-x64-obs33.zip`: `obs-multiview-pro/obs-multiview-pro.dll` + `.pdb` e `obs-multiview-pro/data/locale/*.ini` (layout nuovo di OBS 33, in `C:\ProgramData\obs-studio\plugins\`).
- `obs-multiview-pro-<versione>-windows-x64-obs32.zip`: `obs-multiview-pro/bin/64bit/obs-multiview-pro.dll` + `.pdb` e `obs-multiview-pro/data/locale/*.ini` (layout per OBS 32; OBS 33 lo segna "Legacy"). La DLL e' la stessa dei due zip (compilata contro OBS 32.1.2, provata anche su OBS 33.0.0); il job `release` controlla che ogni zip abbia solo il proprio layout (una DLL in entrambe le cartelle farebbe dire a OBS 33 "skipping legacy plugin").
- `obs-multiview-pro-<versione>-macos-universal.zip`: il bundle `obs-multiview-pro.plugin` zippato con `ditto` (artifact di `build.yml`, rinominato); i file di lingua sono in `Contents/Resources/locale/` e il job macOS fallisce se manca `en-US.ini`.
- `obs-multiview-pro-<versione>-ubuntu-24.04-x86_64.zip`: `obs-multiview-pro/bin/64bit/obs-multiview-pro.so` e `obs-multiview-pro/data/locale/*.ini`.
- `obs-multiview-pro-<versione>-ubuntu-26.04-x86_64-obs33.zip`: `obs-multiview-pro/obs-multiview-pro.so` e `obs-multiview-pro/data/locale/*.ini` (layout nuovo di OBS 33; per OBS 33 la cartella e' `$XDG_DATA_HOME/obs-studio/plugins/`, in genere `~/.local/share/obs-studio/plugins/`, ricavata dal loader di OBS 33 perche' la KB di OBS ancora non la documenta).
- `obs-multiview-pro-<versione>-ubuntu-26.04-x86_64-obs32.zip`: `obs-multiview-pro/bin/64bit/obs-multiview-pro.so` e `obs-multiview-pro/data/locale/*.ini` (per OBS 32, in `~/.config/obs-studio/plugins/`; OBS 33 lo carica come legacy). Stesso `.so` dei due zip 26.04; il job `release` controlla che ogni zip abbia solo il proprio layout.
- `SHA256SUMS.txt`: `sha256sum` dei sei zip.

File di lingua (`data/locale/*.ini`): `target_install_resources` del template li copia nella rundir accanto al plugin (`rundir/<CONFIG>/obs-multiview-pro/locale/` su Windows e Linux) e nel bundle macOS (`Contents/Resources/locale/`). Il job `release` li sposta in `obs-multiview-pro/data/locale/`, la cartella dati che OBS associa a `obs-multiview-pro/bin/64bit/` (`OBSBasic.cpp`, `AddExtraModulePaths`), e fallisce se un artifact Windows o Linux non contiene `en-US.ini`. Senza i file di lingua il plugin funziona ma menu e riquadri mostrano le chiavi (`Menu.Layout`) e il log dice `locale files not found`.

Nessuna action di terze parti: `actions/checkout@v4`, `actions/upload-artifact@v4`, `actions/download-artifact@v4` e `gh` (preinstallato sui runner, token `github.token`).

Per pubblicare: rinominare `## Unreleased` in `## <versione>` nel CHANGELOG, portare `version` di `buildspec.json` alla stessa versione, commit, poi `git tag v<versione>` e push del tag.

Verifiche fatte senza eseguirlo: struttura YAML con un controllo in Python (nessun PyYAML: parser del sottoinsieme usato; job, `needs`, `uses` versionati, `workflow_call` presente), estrazione delle note provata in locale su un CHANGELOG di prova (sezione presente, `v`/`[...]`/testo dopo la versione, sezione mancante o vuota -> errore), passi di packaging e pubblicazione eseguiti in locale con artifact finti e un `gh` simulato con stato (nomi e contenuto degli zip, checksum; tag nuovo -> bozza, upload, controllo, pubblicazione; secondo run su release pubblicata -> errore; bozza parziale -> riusata e completata; bozza con un asset estraneo -> errore prima di pubblicare); estrazione delle note provata anche su un CHANGELOG CRLF con `## ` dentro blocchi ``` e ~~~. Percorsi controllati contro `CMakePresets.json` e gli upload di `build.yml` (`build_x64/rundir/RelWithDebInfo`, `build_macos/rundir/RelWithDebInfo/*.plugin`, `build_x86_64/rundir/RelWithDebInfo/*.so`).

Da controllare al primo tag:

1. Il job `check` legge la versione (jq) e trova la sezione del CHANGELOG.
2. Il workflow riusabile parte con i permessi `contents: read` e i tre job passano come in `build.yml`.
3. Gli artifact scaricati stanno in `artifacts/<nome-artifact>/` con i file attesi (un `.dll` + `.pdb`, un `.plugin.zip`, un `.so`); il job elenca i file e il contenuto degli zip nel log.
4. La bozza viene creata, riceve i 4 asset, il controllo dei nomi passa e la release diventa pubblica. `gh release view <tag>` deve trovare anche una bozza (gh cerca le bozze per nome del tag); se non la trova, un secondo run creerebbe una seconda bozza: in quel caso eliminare la bozza a mano.
5. Ubuntu: `libcurl4-openssl-dev` installato da apt e trovato da `find_package(CURL)`; macOS: `find_package(CURL)` trova la libcurl dell'SDK (come fa OBS stesso) o quella di obs-deps se presente, universale arm64+x86_64.

## Stato e aree mai eseguite

`build.yml` compila e lancia i test unitari su Windows, macOS e Ubuntu a ogni push. I test non richiedono OBS ne' Qt: **solo Windows 11 con OBS 32.2 e 33.0.0 e' stato provato in esecuzione**. Su macOS e Linux il plugin e' compilato (con `-Werror`) ma non e' mai stato avviato dentro OBS.

- Creazione del display in `src/multiview_window.cpp`: macOS (`gs_window.view = (id)winId()`, Metal) e Linux X11 (`gs_window.id` + `obs_get_nix_platform_display()`, OpenGL). Wayland funziona solo con Qt >= 6.9; la release per Ubuntu usa Qt 6.4, quindi sotto Wayland la finestra resta vuota (avviare OBS con `QT_QPA_PLATFORM=xcb`).
- Font dei testi (`Helvetica` su macOS, `Monospace`/`Sans Serif` su Linux): le metriche delle etichette possono differire da Windows.
- Rendering su Metal e OpenGL: solo Direct3D 11 e' stato provato.
- Verifica aggiornamenti: libcurl di sistema su macOS e Linux, mai eseguita.

Se qualcosa si rompe in CI: con `-Werror` il primo errore su macOS/Linux e' di solito un warning clang/gcc (MSVC e' meno severo). Su macOS il job richiede Xcode >= 16.0 (`cmake/macos/compilerconfig.cmake`). Il file `cmake/macos/enable-swift.cmake` serve finche' si compila con i sorgenti di OBS 32.1 (OBS 32.2 abilita Swift da solo).
