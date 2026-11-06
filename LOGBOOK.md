# Logbook NGene Classic

## 6 novembre 2026 - Audit input/ring/canali e prova timeout ritirata

Su richiesta dell'utente verificati ingresso, ring e finestre dei canali.
Harness separato con ring/decoder produzione e tre funzioni esatte estratte
da channelManager: ordine e finestre di otto canali verificati su tutti i
794615808 byte SiGe e 1964112000 byte HackRF, sia letture intere sia frammentate
e vuote. Contratto USB esteso a completamenti fuori ordine, letture parziali
e wrap coda. Nessuna corruzione riprodotta; non e' una prova USB hardware.

Provato limite recupero 3000 contro 5000 ms con stesso binario/RF/config8/4/1.
RF originale: 24 output PNT e 10 perdite in entrambi. Calo artificiale400ms:
3000 ms peggiora recuperi 5 -> 4 e perdite 1 -> 2. Quindi default/template
e configurazione locale provvisori a 3000 sono stati RIPRISTINATI a quelli
precedenti; il solo timeout corto non risolve il difetto. Conservati test
sintetici, override del runner e dati della regressione. SiGe corto:23 fix/0perdite.

Resta il limite documentato: restart SiGe gestito come riallineamento I/Q,
senza propagare il salto ignoto alla timeline GNSS; holdover senza ricerca
del nuovo ritardo. Non dimostrato come causa della caduta live dell'utente.
14/14 CTest nella verifica; build finale normale profiling OFF, main invariato.
Dettagli e riferimenti in development/profiling/input-timeline-20261106/.

## 5 novembre 2026 - Cadute sincrone: verifica dei percorsi esistenti

L'utente segnala Navigation -> Acquisition simultaneo. Verificato che il
recupero intermedio esiste gia' (holdover/relock, limite default 5000 ms) e
HardRestartChannels non ha chiamanti operativi. Un errore PVT non provoca
un riavvio globale RF. Il mantenimento segue gli NCO precedenti e non esegue
una ricerca del nuovo ritardo dopo un salto ignoto di campioni. Il cambio
generazione USB riallinea I/Q ma non ripristina la timeline GNSS.

Nuovo analizzatore separato analyze-tracking-transitions.ps1, nessuna modifica
operativa o al main. Sul replay HackRF storico del 29 ottobre: sette ingressi
in holdover a 77.220 s entro 70.664 ms, cinque Galileo a 77.748 s entro
65.006 ms; le perdite corrispondenti arrivano cinque secondi dopo. Totale
13 perdite, 12 holdover, zero recuperi. Conferma il tentativo di Tracking
prima dell'acquisizione sul salto RF gia' documentato; non identifica la causa
dell'episodio SiGe attuale, per cui e' stato richiesto il record/log specifico.
File SiGe locale breve: zero perdite; dispositivo SiGe assente dall'host.
Contratto tracking esistente PASS, fixture dell'analizzatore verificata.
Dettagli nel README profiling, output in build/tracking-transition-review/.

## 5 novembre 2026 - SiGe Galileo fermo in Tracking

Segnalato dall'utente sia live sia da file. Riprodotto su native-ab.raw:
con IF nativa 38400 Hz i Galileo agganciano ma non decodificano I/NAV.
La IF reale storica equivale a 4130400 - 16367600/4 = 38500 Hz.
Lo scarto di 100 Hz altera il confronto DLL/PLL e puo' provocare correzioni
spurie di 125 Hz, incompatibili con la decodifica dei simboli Galileo.
Corretto default, template e configurazione locale a 38500 Hz; valori espliciti
sempre rispettati. Nessuna modifica ad acquisizione, soglie, loop o parser.

Confronto stesso binario/RF, copie isolate 4 GPS / 4 Galileo / 0 EGNOS:
23 fix in entrambi, ma fix con Galileo 0 -> 13 (fino a due Galileo), correzioni
PLL 4 -> 0, zero perdite RF. Build finale riproduce il risultato, 14/14 CTest,
profiling OFF, main invariato. Runner esteso con override SiGe e sessioni univoche.
Con la configurazione operativa 8/4/1 il record di 48.55 s termina prima delle
effemeridi complete Galileo: ancora 23 fix solo GPS, pur decodificando I/NAV.
La prova live/Linux e l'accuratezza non sono validate da questi conteggi.
Dettagli/hash/log in development/profiling/sige-galileo-20261105/.

## 30 ottobre 2026 - Kalman adattativo, EGNOS e autotune visibile

L'utente richiede un unico modello adattativo, senza opzioni statico/dinamico.
Implementata stima di Q da variazioni della velocita' Doppler al netto della
sua incertezza, piu' stima temporale di R dal residuo del fit ridondante.
Il clock comune e' separato dalla velocita'. Innovazione coerente forte riapre
Q nell'epoca corrente; outage/geometria debole ripristinano incertezza prudente.
Nessuna velocita' forzata a zero. Static Receiver riguarda i controlli LS;
KF usa sempre un controllo cinematico vettoriale, senza allargare la soglia.

Scartata variante Q-only che peggiorava alcune fasi simulate. Versione Q/R:
110/110 contratti, 14/14 CTest; venti semi accoppiati con fermo/accelerazione/
curva/frenata/outage. Da fermo dispersione H/U aggregata 2.500/10.150 ->
1.443/5.835 m, miglioramento medio delle metriche anche nelle fasi dinamiche.
Non tutte le realizzazioni individuali migliorano. WLS identico fra i due
binari del confronto sintetico. Nessuna taratura sulla GT.

Replay misto 24 misurati + 12 previsioni, RMS H/U 4.943/4.696 -> 4.350/4.518 m,
dispersione centrata 4.346/1.552 -> 3.981/1.252 m. GPS 58 + 11: RMS
3.611/4.906 -> 3.723/4.619 m, dispersione 2.054/2.069 -> 1.729/2.227 m:
regressioni orizzontale assoluta e verticale centrata conservate nel rapporto.
R codice e correlazioni dello smoothing restano incompleti; Linux/live non
validati con questi replay. ADAPTIVE_KALMAN_REVIEW.md contiene i limiti.

Su segnalazione EGNOS: decoder ora alimentato anche senza tempo GPS, con
correzioni non temporizzabili escluse. MT0 senza tempo invalida la banca e
mantiene una finestra prudente quando il tempo torna noto. Ncurses distingue
TRACKING / SBAS NO TIME / SBAS DATA / SBAS TEST, senza fingere ranging GEO.
Replay: CRC validi gia' fra 8 e 9 s, prima del primo PVT a 55 s; PRN 123
trasmette MT0. Stato ufficiale consultato: 121/136 operativi, 123 in test.
La scarsa disponibilita' RF live non e' dichiarata risolta. Soglie e logica
di acquisizione invariate.

Autotune HackRF: mancava il tag nel file locale e il default del parser era
0, diverso dal template a 1. Ora default coerente, voci esplicite locali,
0 esplicito rispettato; riga ncurses persistente con avanzamento/risultato,
guadagni e modalita' replay. Corretto falso settled con una sola finestra
accettata al dodicesimo tentativo. Test del ciclo completo con ABI hardware
simulata, isolamento campioni e restart senza ritaratura. Nessuna nuova prova
hardware/Linux della dashboard; esiste il log hardware storico del 29 ottobre.

Dettagli EGNOS/autotune in EGNOS_AUTOTUNE_REVIEW.md; CSV, hash, log e sorgente
baseline in development/profiling/adaptive-kalman-20261030. Main senza nuova
strumentazione, build normale profiling OFF, algebra double Eigen/Joseph.

## 30 ottobre 2026 - Kalman: origine dei clock e covarianza temporale

Chiarimento successivo dell'utente: il problema e' dispersione KF maggiore
del LMS nel live Linux. Aggiunte metriche centrate sulla media, distinte
dall'RMS verso GT: nel replay KF H misto 4.694 -> 4.346 m, GPS 2.163 -> 2.054 m.
Il replay non riproduce quella regressione live rispetto al WLS. Il filtro
resta dinamico a velocita' costante; Static Receiver controlla soltanto i
salti, non seleziona un modello statico. Questo limite resta da affrontare
esplicitamente, senza presentare le correzioni sotto come validazione live.

Ripresa dell'analisi PVT su richiesta dell'utente. Confermati due difetti che
i vecchi casi a innovazione nulla non esercitavano: il bias gia' rimosso da
SetReferenceRxTime veniva riutilizzato nel nominale KF; Q non rispettava la
composizione dei passi temporali. Corretto anche il nominale del clock assente
quando PredictRxTime applica la nuova deriva all'intera eta' del riferimento.

Nominale azzerato solo per il clock appena ribasato; incertezza mantenuta in P.
Q integrata con densita' coerenti, conservando i valori effettivi gia' usati
per velocita' e deriva, inclusa la correlazione dei due bias col drift comune.
Nessuna taratura sulla ground truth, nessuna modifica a main, tracking o
decisioni/soglie di acquisizione. Precisione double e Joseph/Eigen conservate.

Stesso harness finale: sorgente precedente 84 PASS / 7 MISMATCH, corretto
91/91. Ribasamento sintetico di 100 m: spostamento spurio 47.833 m -> 2.64e-9 m.
Ciclo reale tempo/pseudorange/KF con transizioni/outage: 96.007 -> 0.002823 m.
Build operativa normale Windows 14/14 CTest, profiling OFF.

Replay RF diagnostico, configurazione corrente 8 GPS / 4 Galileo / 1 EGNOS,
CSM=8: KF misto 24 misurati + 12 previsioni in entrambi i casi; RMS H/U
5.555/5.426 -> 4.943/4.696 m. GPS 58 misurati + 11 previsioni: lieve
peggioramento 3.547/4.896 -> 3.611/4.906 m. Epoche e conteggi comuni verificati;
coordinate testuali arrotondate, non prova di osservabili identiche.
SiGe KF 23 soluzioni, zero perdite prima/dopo; GT HackRF non applicata a SiGe.

Restano R empirica, correlazioni smoothing/Doppler non modellate, assenza
di gate statistico delle innovazioni, dinamica statica non distinta e P
iniziale indipendente dalla geometria WLS. Linux/live non verificati qui.
Dettagli: KALMAN_REVIEW.md; hash, log, CSV e sorgente baseline in
development/profiling/kalman-review-20261030. Il controllo dell'archivio
instrumentation.patch fallisce su profilingSupport.c gia' non modificato da
questo intervento; nessuna applicazione di quella patch nelle misure.

## 29 ottobre 2026 — Fast fix transizioni tracking e separazione PNT/RF

Considerata l'analisi PDF fornita, verificando le affermazioni sul codice corrente.
Su richiesta dell'utente introdotto mantenimento NCO a breve termine: traiettoria
da fasi/epoche affidabili e frequenze gia' filtrate, rigetto degli ancoraggi nel
rumore, rientro con evidenze ripetute di picco codice, CN0 e poi PLL. Controlli
ogni 100 ms; cinque esiti centrali del false-lock checker evitano l'attesa di
conferma se non c'e' stata correzione. Altrimenti conferma storica completa.
Soglie PLL numeriche invariate; il supervisore usa esplicitamente quella bassa
per trattenere un lock precedentemente consolidato. Primo ingresso nel PLL
ancora subordinato alla soglia alta esistente.
Limite unico dell'episodio, incluse ricadute/conferme: tag opzionale
`[Tracking holdover ms]`, default 5000, 0 disabilita, massimo 10000.

Nessuna misura coasting nel PNT e nessuna decodifica di bit degradati. Reset
del solo arco smoothing e dei frammenti parser; effemeridi/WN conservati, nuovo
TOW richiesto. Questa scelta rallenta realmente la ripartenza PNT: non nasconderlo.
Dieci fallimenti PNT gravi reinizializzano solo lo stimatore, senza riavviare
tracking/effemeridi e senza bypass dei controlli sul risultato successivo.

Scale binarie negative dei parser ora esatte; trovati arrotondamenti soprattutto
in 2^-14/15/16. NeQuick: default Az=63.7 per terna nulla e limiti [0,400].
Verificati altri campi iono/BGD/TOW e interfacce/unita' dei modelli atmosferici;
non e' una validazione integrale NeQuick/ICD. Carrier-phase PVT non aggiunta:
rimangono necessari osservabile continua, ambiguita', slip e archi validati.

Verifiche: 13/13 CTest e 74/74 confronti sintetici. Rumore artificiale 400 ms:
5/5 episodi recuperati, zero perdite RF; 1200 ms: perdite 9 -> 2, sette recuperi.
Il caso breve passa pero' da 13 a 1 output PNT nei 70 s della prova. Sul file
originale con salto noto: misto 29 -> 24 fix, GPS 63 -> 57, perdite RF sempre
13/8. KF misto finale 24 con misure + 12 previsioni. SiGe 23 fix, zero perdite.
Coordinate stampate identiche sulle 23 epoche miste e 40 GPS comuni con uguali
conteggi satellitari. Nessun miglioramento metrico universale dimostrato.

Dettagli e regressioni: TRACKING_STATE_REVIEW.md. Hash/configurazioni/log e CSV:
development/profiling/tracking-review-20261029. Build normale profiling OFF;
patch di profiling ancora applicabile. Main e acquisizione senza differenze.
Eseguibile: 3770A2BAB3FB943D179B19116573637B51B23D1BDF513C0B564E397D9C09E665.
Linux/live, dinamica e discontinuita' esplicite frontend restano da verificare.

## 29 ottobre 2026 — PNT, Eigen, smoothing e PRN EGNOS attuali

Su richiesta dell'utente e dopo confronto Armadillo/Eigen, confermato Eigen.
Sostituite implementazioni matriciali custom con adattatori C a Eigen double:
QR con pivoting, Kalman Cholesky/Joseph, operazioni matriciali e DOP.
Rimossi scratch obsoleti; conversione geodetica LS una volta per iterazione.
WLS selezionabile con `[PNT weighted LS]` (default 1, 0 per LS non pesato).
Messaggi operativi PNT; nomi C e PVT.conf compatibili.

Smoothing: usa Doppler del blocco completato, avvio Hatch progressivo e
variazioni clock/SBAS coerenti con il codice corretto. Non e' stata aggiunta
una soluzione con ambiguita' di fase: il vecchio dato e' Doppler integrato,
non una misura indipendente validata. Restano da gestire cicli/slip/ambiguita'.

EGNOS: aggiunti PRN 121 e 123, generazione G1/G2 ICD verificata byte per byte
sui quattro PRN storici, compatibilita' dei quattro slot del file FFT.
Nel replay diagnostico EGNOS123 ora entra in tracking e aggancia FLL/PLL;
perde lock sul gap noto e viene riacquisito. MT0 invalida la banca correzioni
e ne inibisce l'aggiornamento per 60 secondi dall'ultimo avviso.
Nessuna modifica a decisioni/soglie acquisizione, loop tracking o main.

Verifiche: 11/11 CTest, 54/54 confronti sintetici, replay LS/WLS/KF/SiGe.
WLS contro LS sullo stesso codice: misto RMS H/U 6.137/13.841 -> 5.720/13.117 m;
GPS 3.395/7.068 -> 3.195/7.284 m. Nessun vantaggio universale: rispetto alla
sessione precedente peggiora l'orizzontale GPS. Nel misto 29 fix anziche' 40,
con differente storia satelliti/scheduling dopo l'aggancio EGNOS; non attribuire
la differenza al solo solver. KF misto 29 con misure + 12 previsioni.
SiGe 23 soluzioni/zero perdite. Il nuovo data record non era disponibile.

Rapporto: PNT_REVIEW.md. Risultati/hash/log:
development/profiling/pnt-review-20261029. Build finale profiling OFF;
patch di profiling ancora applicabile. Linux/live non verificati.
Le sezioni storiche sottostanti descrivono il codice prima di questa revisione.

## 28 ottobre 2026 — Correzioni PVT implementate in produzione

Su richiesta esplicita dell'utente, implementati tutti i difetti confermati
dall'audit e la regressione del controllo DOP. Matematica propria in double:
QR esistente per LS, Cholesky/soluzioni triangolari per KF, Joseph mantenuta.
Nessuna nuova dipendenza e nessun codice GNSS-SDR copiato.

- WN/TOW normalizzati, differenze che includono la settimana, ricostruzione
  WN0G vicina, conversione GGTO invalida non marcata come disponibile.
- GM/F per costellazione, velocita' regolare al perigeo, clock rate satellitare
  nei Doppler e rotazione geometrica coerente di posizione/velocita'.
- Health GPS L1 C/A include lo stato del segnale. Corretto anche il flag
  Galileo confuso con il valore numerico del bias durante init/hard reset;
  bias e drift iniziali esplicitamente azzerati.
- Minimo satelliti aggiornato dopo esclusioni; KF non cancella smoothing e
  non elimina le nuove misure singola costellazione. Varianza transitoria
  per canale. Reset PVT soft preserva smoothing, reset timeline/hard lo cancella.
  Nuova origine di una costellazione invalida solo lo storico corrispondente.
- LS controlla convergenza (1e-4 m, massimo 7 passi) e usa Hopfield con quota,
  come la famiglia atmosferica usata dal manager/KF. Nessuna taratura su GT.
- KF usa dt dai campioni e mantiene 9 stati se entrambi i clock sono inizializzati,
  anche in assenza temporanea di una costellazione. Covarianze validate e
  sigma spaziale formale limitata a 100 m. Eliminati 10 buffer inutilizzati
  dell'inversa: 90.3 KiB e relativi azzeramenti a 17 canali.
- DOP geometrico indisponibile distinto da errore KF. Previsione senza misure
  schedulata correttamente e limitata dall'incertezza; NMEA qualita' zero senza
  osservazioni, HDOP vuoto se non disponibile. Non contare previsioni come fix.
  Bias cumulati aggiornati solo per costellazioni osservate, contatore PVT
  non incrementato per il semplice salvataggio di una soluzione respinta.

Verifica finale: 10/10 CTest, 43/43 confronti sintetici su 44 funzioni estratte.
Include transizioni costellazioni, 3 misure, outage, matrice/rumore invalidi,
clock rate, reset e aritmetica temporale. Profiling OFF, patch archiviata ancora
applicabile; main/acquisizione/tracking senza differenze.

Replay finali (stesso binario): LS misto 40 soluzioni; KF misto 40 con misure +
12 previsioni (4 LS/48 KF totali); LS GPS 63; KF GPS 64 con misure + 12 previsioni
(4 LS/72 KF). Vecchio binario configurato KF GPS produce 43 soluzioni, tutte LS:
prima il KF non restava operativo. Confronto sulle 43 epoche GPS comuni, stesso
numero di GPS: RMS H 4.291 -> 2.654 m, U 7.793 -> 7.460 m. E' l'effetto di tutte
le correzioni, non un confronto isolato del filtro. SiGe: 23 soluzioni/0 perdite.

Tracking RF invariato: 14 perdite sul misto, 8 GPS-only, zero hard restart e
zero fallimenti PVT gravi. Nessuna promessa di risoluzione USB/live o di integrita'
durante gap non quantificati. Linux/live resta da verificare; nessun pool DSP.

Binario finale SHA256 6685EEED311091EE9A1EFABF34ECF6DDC3AB532B2BC4BD63BDFEBE684F9E69AB.
Backup locale codice/binario precedente in build/pvt-before-fixes. Risultati,
hash, epoche e riferimenti ai log in development/profiling/pvt-fixes-20261028.
I risultati dell'audit PRIMA restano separati in pvt-audit-20261028.
PSEUDORANGE_PVT_AUDIT.md sezione 5 e OPTIMIZATION_REPORT.md descrivono lo stato.
Configurazioni principali invariate: PVT method 0 LS, 1 KF; runner -PvtMethod
modifica solo la copia della prova. Non riapplicare i vecchi reset smoothing.

## 28 ottobre 2026 — Audit esteso pseudorange, ICD e Kalman

Richiesta: controllare messaggi, pseudorange, orbite, GPS+Galileo, LS e Kalman.
PDF locali GPS200N/Galileo2.2 letti e confrontati. Su indicazione dell'utente,
GNSS-SDR consultato solo per confronto, senza copiarne codice. Riferimento
GNSS-SDR 3fcb502, blocco hybrid_observables_gs. NGene usa ricezione comune,
allineata al ring tramite -bufferOffset/fs, non trasmissione comune; non cambiare
metodo per imitare il concorrente. Due origini iniziali per costellazione sono
compatibili con i due bias stimati nel modello misto.

Audit al commit 4a2bf4e, nessuna modifica di produzione. 39 funzioni estratte
esattamente in harness isolato, 34 confronti: 25 PASS / 9 discrepanze, salvate
in development/profiling/pvt-audit-20261028. Strumenti riutilizzabili separati.
Report dettagliato accanto al README: PSEUDORANGE_PVT_AUDIT.md.

Difetti dimostrati: smoothing KF singolo azzerato ogni epoca e tutte le misure
eliminate; minimo 5 rimasto dopo esclusione dell'ultimo Galileo con 4 GPS validi;
rollover TOW scarta tempi validi; ricostruzione WN0G errata sul bordo di 64
settimane; costante GM Galileo applicata anche a GPS (1.023 m nell'orbita
sintetica a toe+3600); velocita' NaN al perigeo; rotazione terrestre KF
dipendente dal bias della pseudorange; health GPS considera solo MSB.

Regressione del controllo precedente: FALSE di DopEvaluation forza reset anche
quando a Kalman manca soltanto il DOP geometrico con <4/5 misure. Non presentare
questa condizione come corretta o come difetto esclusivamente storico.
Non e' stata corretta qui: occorre distinguerla dagli errori numerici e trattare
la covarianza KF/outage insieme alla gestione misure. Codice attuale non ancora
validato end-to-end per Kalman nonostante i test positivi del solo QR.

Test positivi: LS sintetico GPS/misto a due bias recupera posizione entro 1e-5 m;
selezionati campi GPS/Galileo segno/scala corretti; Joseph gia' presente, zero
innovazione e propagazione covarianza outage verificati. Non accusare il reset
dello stato errore KF senza considerare la reiniezione nel nominale.

Altri limiti: modelli tropo diversi LS/KF, LS fisso 7 passi senza criterio di
convergenza, niente gate statistico residui, derivata clock satellite assente
dai Doppler, inversa LDU ancora nel KF, pesi/URA/SISA e stati integrity da
completare. Le anomalie del rollover non sono dimostrate attive nel record.
Instabilita' RF e gap restano separati. Non dichiarare la catena risolta.

## 28 ottobre 2026 — Controllo mirato PVT / LMS

Richiesta: verificare il PVT, soprattutto LMS, contenendo il round. L'utente
ribadisce che Navigation/Tracking/Acquisition continua a oscillare: non
considerare risolto il tracking da una correzione del solver.

Correzioni: Jacobiano LS normalizzato con distanza geometrica anziche'
pseudorange; QR Givens in double per posizione/velocita' e covarianza DOP,
senza equazioni normali, dipendenze o allocazioni aggiuntive; geometria DOP
ricostruita alla posizione finale, HDOP/VDOP nel piano locale; rigetto esplicito
di rango insufficiente/non finiti con avanzamento alla successiva epoca PVT.
Corretti anche `deltaTime += fabs(deltaTime)` per Galileo, radice della variabile
errata nel controllo dinamico e argomento mancante del relativo log.

Nessun cambiamento ad acquisizione, tracking, soglie di lock o smoothing.
Risolutore ancora LS non pesato, massimo 7 iterazioni; nessuna pretesa di aver
verificato integralmente modelli atmosferici, osservabili e outlier.

Test: nuovo contratto QR (4/5 stati, soluzioni note, residui, covarianza,
condizionamento 1e9, rango insufficiente/non finiti), totale 10/10. Replay
completi sulla diagnosi: misto 40 fix e 14 perdite identiche anche nelle epoche;
GPS-only 63 fix e 8 perdite. Misto RMS H/U 7.430/18.265 m contro 7.413/18.158 m;
GPS-only 2.778/7.346 m, praticamente invariato. Nessun miglioramento metrico
dimostrato; nessuna risoluzione del ping pong. Nel misto zero hard restart,
zero fallimenti PVT gravi, 24 scarti GDOP.

NAVIGATION in ncurses dipende da `usedForPVT`, non e' un nuovo stato del loop:
lo scarto del PVT puo' far apparire TRACKING senza perdita RF. TRACKING ->
ACQUISITION passa da TRACKING_LOST o HardRestartChannels; nel replay e' il
primo percorso. Uscire da TRACKING_LOCKED azzera lo smoothing del canale;
anche ResetPvt soft azzera globalmente lo smoothing: possibile prolungamento
della indisponibilita' PVT, non prova della causa delle perdite di lock.

Nuovi CSV/statistiche in `development/profiling/diagnosi-20261027/pvt-qr-*`.
Log completi: `build/profile-20261027-152510-947` (misto), `...-958` (GPS).
Profiling OFF; strumenti di test separati dall'eseguibile operativo.

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
