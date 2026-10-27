# Logbook NGene Classic

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
