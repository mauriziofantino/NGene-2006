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
