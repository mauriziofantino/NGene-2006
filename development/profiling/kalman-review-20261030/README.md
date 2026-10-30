# Analisi Kalman, 30 ottobre 2026

Rapporto: [KALMAN_REVIEW.md](../../../KALMAN_REVIEW.md).

- `audit-before.csv` e `audit-after.csv`: stesso harness, 91 controlli;
  prima 7 discrepanze, dopo nessuna.
- `source-before.json` e `source-after.json`: hash dei sorgenti estratti,
  header e harness. Il percorso baseline e' esplicito nel manifest.
- `kalmanFilter-before.txt`: sorgente Kalman originale archiviato, non compilato
  dalla build operativa. Permette di riprodurre le discrepanze senza modificare
  i sorgenti di produzione.
- `results.json`: eseguibili, hash RF, run, percorsi locali dei log completi
  e hash di ciascun file di configurazione delle prove.
- `common-mixed.json` e `common-gps.json`: confronto sulle epoche con RxMs
  e conteggi uguali; include anche il sottoinsieme realmente KF nei due run.
- `common-wls-kf-mixed.json`: 14 epoche WLS/KF comuni. I confronti separano
  RMS rispetto al riferimento, media e dispersione intorno alla media.
- Le cartelle dei replay contengono CSV e statistiche. `*-sige/validation.json`
  riporta disponibilita' e perdite, senza applicare il riferimento HackRF.
- `build-and-ctest.log`: build finale normale, 14/14 test, profiling OFF.

Riproduzione dei contratti, da PowerShell nella radice del progetto:

```powershell
# Il primo comando stampa intenzionalmente sette MISMATCH del codice vecchio.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File development/profiling/run-pvt-audit.ps1 -KalmanSource development/profiling/kalman-review-20261030/kalmanFilter-before.txt
powershell.exe -NoProfile -ExecutionPolicy Bypass -File development/profiling/run-pvt-audit.ps1 -Verify

# Stesso eseguibile, solver imposto solo nella copia della prova.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File development/profiling/profile-replay.ps1 -InputFile recordings/diagnosi/hackrf-l1-ci8.raw -Frontend hackrf -PvtMethod 1 -WeightedLs 1 -Repetitions 1
```

`run-pvt-audit.ps1` accetta `-PythonExe` quando Python non e' nel percorso
predefinito dello strumento. Le due esecuzioni dell'audit sovrascrivono i file
temporanei in `build/icd-audit/`; i CSV qui sono le copie archiviate.

Confronto delle coordinate esportate, usando Python disponibile:

```powershell
python development/profiling/compare-pvt-epochs.py development/profiling/kalman-review-20261030/kf-mixed/fixes.csv development/profiling/kalman-review-20261030/kf-corrected-mixed/fixes.csv --output build/kalman-common-mixed.json
```

Le configurazioni delle prove sono 8/4/1, CSM=8, con override GPS-only 8/0/0.
Quelle dei run rimangono in `build/profile-20261030-*/run-1/configuration`.
Nessun profiling esteso e' stato applicato. Il controllo dell'archivio
`instrumentation.patch` segnala un conflitto preesistente in
`utility/profilingSupport.c`; quella patch non e' usata da queste misure.
