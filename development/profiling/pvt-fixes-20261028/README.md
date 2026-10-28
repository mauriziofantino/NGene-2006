# Correzioni PVT — evidenze del 28 ottobre 2026

- `audit-results.csv`, `source-manifest.json`: 43/43 confronti su 44 funzioni
  correnti e hash dei relativi sorgenti/header. Includono scenari di errore,
  cambio costellazione e outage. `-Verify` restituisce errore se un confronto fallisce.
- `ls-mixed/`, `ls-gps/`, `kf-mixed/`, `kf-gps/`: replay completi dello stesso RF.
- `before-kf-gps/`: stesso scenario con eseguibile salvato prima delle correzioni;
  configurato KF ma tutti i 43 risultati erano LS.
- Ogni cartella conserva `run.json`, fix ENU, eventi e statistiche. Le statistiche
  distinguono soluzioni con misure e pura previsione. La vecchia chiave AllFixes
  conta tutti gli output di posizione e non va interpretata come numero di fix RF.
- `matched-gps-epochs.csv`: 43 epoche comuni vecchio/nuovo KF configurato, stesso
  numero di GPS. Il confronto include tutte le correzioni e non isola il filtro.
- `results.json`: metadati, hash del binario e riferimenti ai log integrali locali.

Il vecchio codice e il binario sono conservati localmente in
`build/pvt-before-fixes/`; nessun sorgente temporaneo e' incluso nel main.
La configurazione principale non viene modificata dal runner.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File development/profiling/run-pvt-audit.ps1 -Verify
powershell.exe -NoProfile -ExecutionPolicy Bypass -File development/profiling/profile-replay.ps1 -InputFile recordings/diagnosi/hackrf-l1-ci8.raw -Frontend hackrf -PvtMethod 1 -GpsChannels 8 -GalileoChannels 0 -EgnosChannels 0 -Repetitions 1
```

I tempi in `run.json` sono replay Windows concorrenti e non misurano un guadagno
CPU sul Linux dell'utente. Ground truth riferita alla sola cattura HackRF.
Nessuna prova live USB. Nessun codice GNSS-SDR copiato o adattato.

Vedere [il report](../../../PSEUDORANGE_PVT_AUDIT.md) per modello, correzioni,
incertezza della previsione e problemi ancora aperti.
