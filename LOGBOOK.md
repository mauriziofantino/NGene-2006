# Logbook NGene Classic

## 27 ottobre 2026 — Diagnosi RF e verifica acquisizione per satellite

L'utente conferma configurazione Linux identica al repository: 8 GPS, 8 Galileo,
1 EGNOS. Il 73% e' la percentuale del processo in top/htop; normalmente esprime
0.73 CPU logiche, ma questo non dimostra equivalenza col vecchio 13% SiGe.

Vincolo chiarito: mantenere la logica della catena di acquisizione del singolo
satellite della prima versione SiGe IF. Non si riferisce allo scheduling delle
costellazioni. La proposta di togliere la quota Galileo era un fraintendimento:
**non e' stata applicata**, lo scheduling resta quello di HEAD.

Confronto effettivamente eseguito col primo commit `efcdb86`, disponibile nel
repository (non con una cartella esterna Old NGene Code):

- Stesse fasi coarse FFT -> raffinamento Doppler -> raffinamento codice ->
  conferma M-su-N -> tracking; stessi criteri e avanzamenti dei bin.
- `manageAcqConfiguration.c`, `nonCoherentSumsManager.c`, `acqConfiguration.h`
  e il file campione `Acquisition.conf` senza differenze dal primo commit.
- Cambiati ingresso/mixer I/Q, aritmetica e scaling HackRF. La stima del rumore
  di conferma HackRF usa campioni I/Q reali, mentre il ramo storico SiGe usa
  ancora i vecchi carrier. Non affermare che le soglie siano identiche.
- Nessuna nuova modifica a fasi, griglie, integrazioni o conferma in questa
  diagnosi. L'unico intervento DSP corrente interrompe le FFT HackRF gia'
  saturate che il ciclo di scaling scarterebbe comunque. Output accettati
  identici verificati con riferimento scalare e replay completo.

Registrazione `recordings/diagnosi/hackrf-l1-ci8.raw`: hash verificato,
1,964,112,000 byte, 120 s a 8,183,800 campioni complessi/s. Oltre il 65% di
ciascuna componente e' esattamente a fondo scala. Una sonda indipendente in
double (senza DSP/ring del ricevitore) rileva un salto comune della fase codice
GPS 5/7 intorno a 77.073 s, circa 350 campioni modulo periodo. Non quantifica
campioni persi assoluti e non identifica il responsabile hardware/USB.
Il replay perde il lock comune dopo circa 1 s, anche solo GPS. Il ring reale
preserva tutti i byte in due modalita' di lettura: nessuna corruzione riprodotta
in questi percorsi. Questo non assolve la catena live.

Log Linux live: 137 fix, zero perdite finali, due warning PLL recuperati;
interruzione USB con coda massima 295662/1048576 byte (28.2%), non overflow
applicativo. L'errore di stop successivo e' secondario. Questo log e la cattura
RF sono sessioni diverse. Causa USB ancora aperta.

Ground truth WGS84 dell'utente: lat 45.06538716666666, lon 7.659323472222223,
h ellissoidica 311.9349 m; offset rispetto al riferimento antenna non fornito.
RMS orizzontale/verticale: live 7.612/8.496 m (137 fix), replay misto
7.413/18.158 m (40 fix), solo GPS 2.778/7.346 m (63 fix). Campioni temporali e
satelliti diversi: non concludere che Galileo sia difettoso da questi numeri.

FFT: microbenchmark alternato nello stesso processo, mediana 173.945 ->
103.823 us per ricerca di scaling (-40.3% per quel kernel). I tempi completi
sono molto variabili e anche contraddittori fra sessioni: nessuna percentuale
di accelerazione complessiva affidabile. I 928 eventi GNSS ordinati dei replay
misti prima/dopo coincidono, inclusi 40 fix e 14 perdite. Verifiche scalari FFT
su 263056 casi; build finale normale senza profiling, 9/9 test.

Strumenti e risultati ripetibili in `development/profiling/diagnosi-20261027/`
e nel README del laboratorio. Il report accanto al README contiene il dettaglio.
Nessuna strumentazione aggiuntiva e' rimasta nel main. Il nuovo lavoro non
introduce worker DSP: il parallelismo produttivo resta un intervento successivo.

## 26-27 ottobre 2026 — Profiling, semplificazione e budget acquisizione

Richieste: indagare tracking che torna in acquisizione e possibile problema USB;
ragionare sulla distribuzione del carico tra core; eliminare Java GUI e IFEN;
mantenere produzione essenziale e precisione; conservare gli strumenti di
sviluppo separati e documentare il lavoro per le sessioni successive.

Letture e risultati principali:

- USB ha gia' un thread dedicato. I manager dei canali, i parser e PVT sono
  sequenziali. Il backup registri SIMD e numerosi buffer sono globali/condivisi:
  un semplice parallel-for sui canali non e' corretto.
- Nel percorso SiGe il timeout riavvia lo stream; la generazione risincronizza
  I/Q ma non invalida tutta la timeline di tracking/PVT. HackRF interrompe invece
  la ricezione su overflow. Nessuna causa live e' stata confermata: SiGe assente.
- I costi totali riflettono il tempo passato negli stati, oltre al costo del
  kernel. Il replay HackRF solo GPS produce 29 fix anche quando quello misto
  ne produce zero e consuma meno CPU: serve anche un budget di ricerca equo.

Modifiche operative:

- Rimossi server/protocollo/configurazione Java, spettro/istogramma della GUI,
  export IFEN e harness di replay dei soli messaggi in formato IFEN. Restano
  testo/ncurses, replay RF, RINEX/NMEA, navigazione e autenticazione GNSS.
- Galileo usa `otherData/Galileo_real_PRNs.txt`; eliminata l'opzione generatore
  di ricerca di tutti i PRN e il fallback implicito. Lista assente, vuota o con
  PRN fuori intervallo produce errore di startup quando Galileo e' abilitato.
- Un solo tentativo Galileo contemporaneo finche' restano canali GPS da riempire.
  I tentativi includono ricerca, raffinamento e conferma. Contatore aggiornato
  a ogni assegnazione e rilascio. In Galileo-only il budget resta disponibile.
- Tolta l'affinita' rigida del main al processore logico 1. Non e' stato attivato
  un pool DSP: la distribuzione proposta richiede finestre possedute e scratch
  privato. Il frontend mantiene il suo comportamento di scheduling esistente.
- Nessuna riduzione della precisione numerica, nessuna modifica a soglie di
  lock, smoothing, discriminatori o algoritmi PVT.

Confronti con profiling temporaneo, stessa configurazione mista 8 GPS/8 Galileo/
1 EGNOS e stessa lista PRN: prima della quota 0 fix su entrambi i file; dopo la
quota 29 fix HackRF e 23 SiGe. Nessuna perdita di tracking riportata. La CPU puo'
aumentare perche' il lavoro utile e il numero di canali tracciati aumentano.
Solo GPS HackRF produce 29 fix con un diverso insieme di satelliti rispetto
alla prova mista: le coordinate non sono un confronto di accuratezza equivalente.

Il profiling esteso e' stato spostato in
`development/profiling/instrumentation.patch`; il runner e le istruzioni sono
nella stessa cartella. La patch viene verificata con `git apply --check`.
`build.cmd` disattiva esplicitamente il profiling. Vedere il report per misure,
artefatti e verifica conclusiva.

Verifica conclusiva: build operativa Windows con `NGENE_RX_PROFILING=OFF`,
9/9 test; 23 fix SiGe e 29 HackRF. Nessun CSV di profiling. Le 428 righe GNSS
SiGe e le 524 HackRF coincidono con quelle della versione strumentata dopo
rimozione di timestamp/metadati e dei tre messaggi di profiling. Lista Galileo
assente, vuota e PRN 51 danno tutti exit code 1. La patch di backup si applica
correttamente in verifica. Dettagli e hash nel report accanto al README.

Discussa anche l'alternativa due thread di acquisizione per costellazione o
2 GPS + 2 Galileo alternati: non implementata in questa sessione. L'utente ha
chiesto di concludere prima il lavoro gia' previsto. Ripartire dalla separazione
dei dati e dai budget di tempo, non da un parallel-for sui manager attuali.

Priorita' successive:

1. Prova live con occupazione coda, timeout/generazione e motivi delle perdite
   correlati all'epoca campione. Non considerare risolto il problema USB dal replay.
2. Discontinuita' esplicite che invalidino job e timeline, senza riavvii trasparenti.
3. Separazione DSP/commit, buffer privati e finestre di acquisizione indipendenti;
   poi pool tracking e budget worker acquisizione misurati su 1/2/4 core.
4. Modalita' fast con sinusoidi squadrate opzionale per acquisizione, solo dopo
   confronto di sensibilita', falsi allarmi e scaling delle soglie. Precise
   rimane il riferimento; il tracking non va approssimato senza dati.

Non sono state verificate qui le presunte identita' con `CODE/Old NGene Code`:
quel confronto e' stato fornito dall'utente, non ripetuto in questo workspace.
