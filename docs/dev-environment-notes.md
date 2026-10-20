# Note sull'ambiente di sviluppo (Windows)

## qlalr.exe e l'antivirus

Le dipendenze Qt di OBS (`.deps/obs-deps-qt6-*/`) contengono `bin/qlalr.exe`, un generatore di parser usato solo per costruire Qt stesso. Il plugin non lo esegue mai: CMake controlla soltanto che il file esista (`Qt6CoreToolsTargets*.cmake`, `_cmake_import_check_files_for_Qt6::qlalr`).

Su macchine con un antivirus che elimina `qlalr.exe`, la configurazione fallisce. Soluzione senza eccezioni all'antivirus: al suo posto si mette un **file di testo segnaposto** con lo stesso nome (nessun eseguibile, quindi niente da bloccare):

```
printf 'placeholder: never executed\n' > .deps/obs-deps-qt6-2025-08-23-x64/bin/qlalr.exe
```

Non estrarre di nuovo il vero `qlalr.exe` dall'archivio. Se le dipendenze vengono riestratte, ricreare il segnaposto prima di configurare.
