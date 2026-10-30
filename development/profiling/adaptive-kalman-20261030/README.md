# Kalman adattativo, EGNOS e autotune - 30 ottobre 2026

Rapporti: [ADAPTIVE_KALMAN_REVIEW.md](../../../ADAPTIVE_KALMAN_REVIEW.md) e
[EGNOS_AUTOTUNE_REVIEW.md](../../../EGNOS_AUTOTUNE_REVIEW.md).

- `audit-results.csv` e `source-manifest.json`: 110 contratti del sorgente finale.
- `synthetic/motion-comparison.json`: venti traiettorie con semi prefissati,
  confronto accoppiato WLS/KF fisso/KF adattativo. Le CSV riportano il primo
  seme, incluse epoche con sola previsione. Le statistiche escludono queste ultime.
- `kf-mixed/` e `kf-gps/`: versione adattativa finale del modello, prima dei
  successivi interventi EGNOS/UI; `final-kf-mixed/` verifica il binario combinato.
- `initial-q-only-mixed/` e `motion-adaptive.csv`: prima variante Q-only,
  scartata. `motion-adaptive-r.csv` conserva la verifica preliminare Q/R.
- `results.json`: hash, eseguibili e riferimenti alle sessioni isolate.
- `egnos-evidence.txt` e `egnos-last-arc.txt`: evidenze del decoder e del blocco
  MT0; il log di correzioni del parser corrente conserva soltanto l'ultimo arco.
  Lo stdout completo di sessione conserva anche gli eventi degli archi precedenti.
- `build-and-ctest.log`: build operativa senza profiling e 14/14 test.

Gli artefatti del KF a rumore fisso immediatamente precedente sono in
`../kalman-review-20261030/kf-corrected-*`. Il sorgente corrispondente e'
archiviato qui in `kalmanFilter-fixed.txt`. Gli helper adattativi eventualmente
aggiunti dall'estrattore sono dichiarati nei manifest; il vecchio filtro non
li chiama. Nessuna verita' nota entra nel filtro di produzione.

Comandi dalla radice:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-native-windows.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File development/profiling/run-pvt-audit.ps1 -Verify
powershell.exe -NoProfile -ExecutionPolicy Bypass -File development/profiling/run-pvt-audit.ps1 -MotionSeries
```

Per riprodurre il confronto sintetico, compilare separatamente le due versioni
e conservare gli eseguibili temporanei prima della successiva compilazione:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File development/profiling/run-pvt-audit.ps1 -KalmanSource development/profiling/adaptive-kalman-20261030/kalmanFilter-fixed.txt -MotionSeries
Copy-Item build/icd-audit/audit-pvt.exe build/icd-audit/audit-fixed.exe
powershell.exe -NoProfile -ExecutionPolicy Bypass -File development/profiling/run-pvt-audit.ps1 -MotionSeries
Copy-Item build/icd-audit/audit-pvt.exe build/icd-audit/audit-adaptive.exe
python development/profiling/compare-motion-models.py build/icd-audit/audit-fixed.exe build/icd-audit/audit-adaptive.exe --output build/motion-comparison --trials 20
```

`run-pvt-audit.ps1` accetta `-PythonExe` se Python non e' nel suo percorso
predefinito. I file in `build/icd-audit` sono temporanei e vengono sovrascritti.

Replay combinato con log dei messaggi soltanto nella copia isolata:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File development/profiling/profile-replay.ps1 -InputFile recordings/diagnosi/hackrf-l1-ci8.raw -Frontend hackrf -PvtMethod 1 -WeightedLs 1 -NavigationLog -Repetitions 1 -TimeoutSeconds 300
```

Il risultato non e' una nuova acquisizione Linux/live, una prova hardware
recente dell'autotune o una certificazione SBAS. Il record contiene il salto
RF gia' documentato. Il riferimento HackRF non va applicato al file SiGe.
