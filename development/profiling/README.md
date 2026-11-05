# Strumenti di sviluppo, esclusi dalla produzione

Questa cartella conserva il lavoro temporaneo del 26-27 ottobre 2026. Nessun
file qui viene compilato o installato da CMake. Il main operativo mantiene solo
il profiling storico, disattivato nella build normale.

`instrumentation.patch` ripristina il profiling esteso di ingresso, lavoro DSP,
stati separati per costellazione, campioni avanzati e massimi di latenza. Include
la correzione del writer CSV Windows (posizionamento a fine file prima di
controllare se scrivere l'header) e il controllo dell'apertura del file.

Da PowerShell, nella radice del repository, con working tree salvata:

```powershell
git apply --check development/profiling/instrumentation.patch
git apply development/profiling/instrumentation.patch
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-native-windows.ps1 -Profiling
powershell.exe -NoProfile -ExecutionPolicy Bypass -File development/profiling/profile-replay.ps1 -InputFile recordings/native-ab.raw -Frontend sige
powershell.exe -NoProfile -ExecutionPolicy Bypass -File development/profiling/profile-replay.ps1 -InputFile recordings/hackrf-l1-vga56-ci8.raw -Frontend hackrf
```

Il runner crea copie isolate di eseguibile, configurazione e dati in
`build/profile-*/run-*`. Salva CSV, stdout, stderr, hash dell'eseguibile, tempo
wall e CPU del processo, exit code. Per confrontare il carico solo GPS aggiungere
`-GpsChannels 8 -GalileoChannels 0 -EgnosChannels 0`; questi override richiedono
configurazione automatica e modificano soltanto la copia della prova.

Per confrontare i due percorsi SiGe usare `-SigeNativeIq 0` oppure
`-SigeNativeIq 1`; `-SigeIqIfHz 38500` modifica soltanto l'IF della copia.
I nomi delle sessioni includono un identificatore univoco per prove concorrenti.
La diagnosi Galileo del 5 novembre e' in
[sige-galileo-20261105](sige-galileo-20261105/README.md).

Dopo le misure:

```powershell
git apply -R --check development/profiling/instrumentation.patch
git apply -R development/profiling/instrumentation.patch
.\build.cmd
```

La build normale imposta esplicitamente `NGENE_RX_PROFILING=OFF`. Prima di
applicare o rimuovere la patch verificare il diff: modifiche future al main
possono richiedere un aggiornamento della patch. Non forzarla se il controllo
fallisce. Per Linux usare il flag CMake `NGENE_RX_PROFILING`; il runner incluso
e' specifico Windows.

Le righe di stato contano anche invocazioni senza elaborazione. `Advanced channel
samples` misura l'avanzamento della timeline del canale, non la quantita' di
FFT: i blocchi integrati, i bin e le somme dipendono dallo stato. Le righe dei
kernel preesistenti sono annidate e non vanno sommate alle righe aggregate.
Il tempo monotono include attese e deschedulazioni; il massimo non e' una misura
di puro tempo CPU. I contatori aggiungono overhead e non sostituiscono una prova
USB live. Usare un CSV nuovo a ogni esecuzione.

Il report completo e le questioni aperte sono in [OPTIMIZATION_REPORT.md](../../OPTIMIZATION_REPORT.md)
e [LOGBOOK.md](../../LOGBOOK.md).

## Diagnosi RF del 27 ottobre

Risultati derivati, ground truth, hash e riferimenti alle esecuzioni sono in
[diagnosi-20261027](diagnosi-20261027/README.md). Nessuno degli strumenti seguenti
e' incluso nella build operativa. Comandi dalla radice, PowerShell:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File development/profiling/build-diagnostic-tools.ps1
& build/diagnostic-tools/check-hackrf-ring.exe recordings/diagnosi/hackrf-l1-ci8.raw
& build/diagnostic-tools/bench-fft-trials.exe recordings/diagnosi/hackrf-l1-ci8.raw
& build/diagnostic-tools/probe-gps-code-phase.exe recordings/diagnosi/hackrf-l1-ci8.raw otherData/GPS_codes.dat 7 1030 77 77.1 0.001
& build/diagnostic-tools/probe-gps-code-phase.exe recordings/diagnosi/hackrf-l1-ci8.raw otherData/GPS_codes.dat 5 -2900 77 77.1 0.001
& development/profiling/measure-ci8.ps1 -InputFile recordings/diagnosi/hackrf-l1-ci8.raw -OutputDirectory build/iq-analysis
& development/profiling/analyze-gnss-log.ps1 -LogFile recordings/diagnosi/ngene-live.log -OutputDirectory build/position-analysis -ReferenceLatitude 45.06538716666666 -ReferenceLongitude 7.659323472222223 -ReferenceHeight 311.9349
```

`check-hackrf-ring` confronta tutto il flusso del ring produzione con un lettore
indipendente, con due sequenze di lettura. `bench-fft-trials` alterna nello stesso
processo il completamento delle FFT sature e l'interruzione anticipata,
verificando guadagno accettato e output identici.

`probe-gps-code-phase` usa un matched filter e mixer indipendenti in double;
legge solo i chip GPS dal file del progetto. Finestra e frequenza campionamento
sono specifiche di questa registrazione (8184 campioni / 8.1838 MS/s). Il segno
della frequenza NCO segue la rotazione del programma, non una convenzione
Doppler universale. Prima misura un ritardo sintetico noto. Un salto della fase
modulo 1 ms non fornisce il numero assoluto di campioni persi.

`measure-ci8.ps1` calcola distribuzioni, saturazione e finestre di un secondo.
`analyze-gnss-log.ps1` produce fix ENU, eventi e statistiche. Conta i fix validi
dal blocco di posizione, non dal numero di invocazioni PVT nel profiling.
`plot-gnss-errors.ps1` esporta scatter ENU e errori di quota PNG usando gli array
PowerShell `-CsvFiles @('file1.csv','file2.csv') -Labels @('serie1','serie2')` e
`-OutputDirectory build/figures`. Il live e il replay vanno etichettati come
sessioni RF diverse.

## Verifica delle correzioni PVT

L'harness `run-pvt-audit.ps1 -Verify` compila funzioni estratte dal sorgente
corrente e confronti sintetici indipendenti, fuori dalla build operativa.
I risultati prima delle correzioni restano in `pvt-audit-20261028/`; quelli
successivi in [pvt-fixes-20261028](pvt-fixes-20261028/README.md).

`profile-replay.ps1 -PvtMethod 0` forza LS nella copia isolata; `-PvtMethod 1`
forza Kalman. Gli override non cambiano la configurazione della radice.
`analyze-gnss-log.ps1` distingue le statistiche MeasurementSolutions e
PredictionOnlySolutions e salva il solver in ogni riga CSV. AllFixes conserva
il nome storico e include entrambe le categorie: non sommare le previsioni ai
fix basati su nuove osservazioni.

## Transizioni tracking e atmosfera, 29 ottobre

Risultati del fast fix e limiti in
[tracking-review-20261029](tracking-review-20261029/README.md). Il generatore
`inject-ci8-outage.py` sostituisce una finestra con rumore CI8 deterministico,
preservando numero dei campioni e byte successivi. Non simula un gap USB.
Il runner puo' abilitare soltanto nella prova il log storico delle correlazioni
con `-TrackingLog`. I contratti separati tracking/atmosfera portano CTest a 13
verifiche; l'audit estratto passa a 74 casi. Nessuna strumentazione nel main.
