# Transizioni tracking, 29 ottobre 2026

Report e limiti: [TRACKING_STATE_REVIEW.md](../../../TRACKING_STATE_REVIEW.md).
`results.json` conserva i run finali/baseline, gli hash e i percorsi dei log
integrali locali in `build/`. I file RF artificiali restano in `build/`;
`fade400.json` e `fade1200.json` ne conservano costruzione e hash.

`audit-results.csv` e `audit-source-manifest.json`: 74 confronti indipendenti,
con funzioni estratte esattamente dal sorgente. I nuovi CTest tracking/atmosfera
sono separati dall'eseguibile operativo. `build-and-ctest.log` e `ctest.log`
conservano l'esito 13/13. `common-epochs.json` confronta solo epoche con RxMs
e conteggi delle costellazioni uguali, su coordinate testuali arrotondate.

Riproduzione della perturbazione CI8, con Python disponibile:

```powershell
python development/profiling/inject-ci8-outage.py recordings/diagnosi/hackrf-l1-ci8.raw build/fade1200.raw --duration 1.2
powershell.exe -NoProfile -ExecutionPolicy Bypass -File development/profiling/profile-replay.ps1 -InputFile build/fade1200.raw -Frontend hackrf -Repetitions 1
```

Per 400 ms omettere `--duration 1.2`. Il generatore non sovrascrive un file
esistente. Il runner puo' abilitare il log storico delle correlazioni soltanto
nella copia di prova con `-TrackingLog`. Non attiva RX_PROFILING e non modifica
la configurazione operativa. Le perturbazioni preservano il numero di campioni;
non simulano un salto della timeline. Le previsioni KF non sono nuovi fix.
