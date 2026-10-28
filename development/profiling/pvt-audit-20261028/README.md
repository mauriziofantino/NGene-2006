# Evidenze audit PVT — 28 ottobre 2026

`audit-results.csv`: 34 confronti sintetici, 25 PASS / 9 MISMATCH rispetto a
riferimenti indipendenti o proprieta' richieste. Le righe MISMATCH sono difetti
osservati, non test corretti artificialmente per riuscire. Due righe riguardano
lo stesso reset smoothing. Exit code 0 significa esecuzione dell'harness riuscita.

`source-manifest.json`: hash e nomi delle 39 funzioni NGene compilate. Sono
estratte dal sorgente corrente; l'estrattore deve fallire se non trova le funzioni.
Il codice generato e' in `build/icd-audit/ngene-audit-extracted.c`.

Ripetizione dalla radice in PowerShell:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File development/profiling/run-pvt-audit.ps1
```

Richiede Windows, Visual Studio clang-cl, dipendenze gia' preparate dalla build
del progetto e Python 3. Il runner accetta `-PythonExe` per scegliere Python;
il default usa la copia locale della cache vcpkg. Non richiede librerie Python.
L'estrazione dei PDF effettuata durante la revisione e' distinta dall'harness.

Servizi stub: logging/shutdown, correzioni EGNOS e IPP, reset esterno PVT.
Quindi l'harness non verifica EGNOS, RF, USB, CRC/FEC o side effect del manager.
Il reset smoothing e la selezione delle misure invece sono le funzioni originali.
La fixture LS usa il modello tropo NGene per le misure sintetiche e non misura
l'accuratezza fisica atmosferica. L'orbita di riferimento e' trascritta dagli ICD.

Nessun codice GNSS-SDR e' stato copiato. Nessun sorgente operativo e' stato
modificato in questo audit. Vedere [il report completo](../../../PSEUDORANGE_PVT_AUDIT.md).
