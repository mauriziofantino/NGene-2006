# PNT / Eigen / SBAS, 29 ottobre 2026

La revisione completa e' in [PNT_REVIEW.md](../../../PNT_REVIEW.md).

- `results.json`: hash del binario finale e dei singoli replay, percorsi dei log,
  configurazioni, conteggi, risultati e limiti.
- `audit-results.csv`, `source-manifest.json`: 54 confronti sintetici su 44
  funzioni di produzione estratte, inclusi LS/WLS misto e GPS, orbite/clock,
  transizioni e outage KF. Compilazione con la libreria numerica produttiva.
- `mixed-*`, `gps-*`: CSV ENU e riassunti dei replay diagnostici precedenti.
- `compare-*.json`: confronti LS/WLS su epoche comuni e uguali conteggi delle
  costellazioni; i conteggi non identificano i PRN.
- `ctest.log`: build operativa con 11/11 test, profiling OFF. Il test aggiunto
  verifica codici SBAS, sequenza di ricerca, MT0, Hatch, clock e sample timing.

Ripetizione dalla radice, dopo `build.cmd`:

```powershell
powershell -File development/profiling/run-pvt-audit.ps1 -Verify
powershell -File development/profiling/profile-replay.ps1 `
  -InputFile recordings/diagnosi/hackrf-l1-ci8.raw -Frontend hackrf `
  -Repetitions 1 -GpsChannels 8 -GalileoChannels 8 -EgnosChannels 1 `
  -PvtMethod 0 -WeightedLs 1
```

Usare `-WeightedLs 0` per il riferimento LS sullo stesso codice, `-PvtMethod 1`
per Kalman. Il runner modifica copie isolate delle configurazioni e registra
anche l'override dei pesi. Le prove non aggiungono profiling al main.

Il record RF nuovo non era disponibile. Nessuna equivalenza live/Linux o
garanzia di incremento dell'accuratezza e' dedotta da queste prove.
