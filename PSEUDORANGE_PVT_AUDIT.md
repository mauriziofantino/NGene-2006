# Audit pseudorange, navigazione e PVT — 28 ottobre 2026

**Revisione successiva, 29 ottobre:** [PNT_REVIEW.md](PNT_REVIEW.md) descrive
l'adozione di Eigen richiesta/confermata dall'utente, WLS, nuove correzioni
del carrier smoothing e PRN SBAS 121/123. Le implementazioni proprie QR e
Cholesky descritte nella sezione 5 sono state sostituite dalla libreria.
Le prove successive non dimostrano un miglioramento metrico universale.

**Stato aggiornato:** le correzioni confermate sono ora implementate. La
[sezione 5](#5-correzioni-implementate-e-verifiche-finali) descrive il codice
finale e le verifiche; le sezioni precedenti conservano l'audit dello stato
iniziale, inclusa la regressione DOP individuata e poi corretta.

## Esito e perimetro

La catena contiene errori riproducibili anche indipendenti dal solver LS.
I problemi piu' urgenti sono nell'uso delle misure da parte di Kalman, nella
gestione delle costellazioni dopo le esclusioni e nei riferimenti temporali
ai passaggi di settimana. Il metodo generale di formazione delle pseudorange
non richiede di essere sostituito con quello di un altro ricevitore.

La prima fase e' stata **un audit**, senza modificare i sorgenti operativi.
Le anomalie iniziali sono documentate sotto; la sezione 5 ne descrive le
successive correzioni. Gli strumenti diagnostici restano in
`development/profiling/`, fuori da CMake. Il problema RF live resta aperto.

Riferimento esaminato: commit NGene
`4a2bf4eaeea07a906b4e7f1ac3f892526cf8efb1`.
Sono stati compilati 39 corpi di funzione estratti automaticamente dai sorgenti
correnti, senza riscriverne la matematica, contro fixture sintetiche. Su 34
confronti: **25 corrispondono al riferimento, 9 mostrano discrepanze**.
Due discrepanze riguardano lo stesso problema di smoothing; i conteggi non sono
il numero di difetti indipendenti. Non sono i test CTest della produzione.

Risultati e hash dei sorgenti: [evidenze](development/profiling/pvt-audit-20261028/README.md).

## Documenti, fonti e indipendenza del codice

Sono stati letti anche i due PDF locali indicati dall'utente, estratti in
`build/icd-audit/` con PyMuPDF gia' disponibile nel sistema. Tabelle di bit e
health sono state controllate anche sulle pagine renderizzate.

| Documento locale | SHA-256 | Sezioni usate |
| --- | --- | --- |
| IS-GPS-200N.pdf | `54ec544bfe7e6acd97daaa1de0ca248e5abec6b418f23c1a69991e5c7bcf749a` | 20.3.3.3.3.1-.2, 20.3.3.4.3, Tabelle 20-IV e 20-VIII |
| Galileo_OS_SIS_ICD_v2.2.pdf | `1ca51b26c970140d2a5fd932fdf6086e5dcf945cbe216ccb081c300136504671` | Tabelle 42-46, 67-73, sezione 5.1.8 |

Copie ufficiali: [GPS IS-GPS-200N](https://www.gps.gov/sites/default/files/2025-07/IS-GPS-200N.pdf),
[Galileo OS SIS ICD 2.2](https://www.gsc-europa.eu/sites/default/files/sites/all/files/Galileo_OS_SIS_ICD_v2.2.pdf).
Le prescrizioni tecniche degli ICD sono criteri di verifica, non istruzioni
operative da eseguire nell'ambiente dell'utente.

Esistono spiegazioni pubbliche del calcolo delle pseudorange e del posizionamento:
[ESA, Code Based Positioning](https://gssc.esa.int/navipedia/index.php/Code_Based_Positioning_(SPS))
e [ESA, Kalman Filter](https://gssc.esa.int/navipedia/index.php/Kalman_Filter).
Gli ICD definiscono segnali, epoche, parametri e algoritmi broadcast; non
impongono l'intera organizzazione del software del ricevitore.

GNSS-SDR e' stato consultato solo come confronto, su indicazione dell'utente:
[documentazione Observables](https://gnss-sdr.org/docs/sp-blocks/observables/)
e [hybrid_observables_gs.cc, commit 3fcb502](https://github.com/gnss-sdr/gnss-sdr/blob/3fcb502aeb618968cd24492010fb8186f122efda/src/algorithms/observables/gnuradio_blocks/hybrid_observables_gs.cc#L713).
Nessun suo codice e' stato copiato, adattato o incluso in NGene o nelle fixture.
La sola routine orbitale di riferimento delle fixture e' una trascrizione
indipendente delle equazioni ICD, con soluzione Newton di Keplero.

## 1. Quale metodo usa NGene

NGene usa un **tempo comune di ricezione**, associato alla coda del ring
`rx->buf.discardedSamples`. `ComputeTransmissionTime` ricostruisce per ogni
canale il tempo di trasmissione e lo riporta a questa epoca:

```text
t_tx(s) = TOW_nav(s) + indice_bit(s) * durata_bit
          + contatore_ms(s) / 1000 + fase_frazionaria_codice(s) / f_codice
          - bufferOffset(s) / fs
```

Il termine finale allinea canali che hanno elaborato quantita' di campioni
diverse. Il caso sintetico con 8184 campioni di differenza e 20 ms fra gli
indici dei bit recupera la differenza attesa entro 1e-10 s. E' un controllo
dell'allineamento algebrico, non una validazione della timeline dopo un gap USB.

Con tempo ricevitore disponibile, `ComputePseudoranges` equivale a:

```text
P_raw(s) = c * (t_rx - t_tx(s))
P_corr(s) = P_raw(s) + c * correzione_clock_satellite(s) + correzione_fast
```

Senza tempo ricevitore, viene assegnato al satellite col t_tx piu' recente
un tempo convenzionale di volo: 70 ms GPS, 78 ms Galileo. Gli altri range
sono relativi a questo riferimento. La scelta iniziale non e' un errore di
per se': l'offset comune viene stimato nel bias di clock.

Non e' il metodo a tempo comune di trasmissione, che confronta le diverse
epoche di arrivo di una stessa epoca trasmessa. GNSS-SDR appartiene anch'esso
alla famiglia con ricezione comune, ma interpola le osservabili e gestisce un
contatore comune di ricezione: non ne deriva identita' delle implementazioni.

In NGene l'origine iniziale puo' differire fra le costellazioni. I due bias
LS assorbono queste origini, oltre alla differenza di sistema/inter-system bias.
Non si deve imporre un solo bias senza trasformare coerentemente le osservabili.
Per il misto servono tre coordinate e due bias: almeno cinque misure e rango
sufficiente. Cinque satelliti non garantiscono da soli una geometria utile.

## 2. Difetti dimostrati con casi riproducibili

### A. Kalman singola costellazione elimina le proprie misure — priorita' alta

`pvt/carrierSmoothing.c:123`: quando e' attivo Kalman e una costellazione ha
zero satelliti, `PerformCarrierSmoothing` esegue `ClearCsmData` a ogni epoca.
Il contatore torna a zero e dopo lo smoothing vale 1, anche partendo da 20.

`SelectChannelsForPvt` assegna a Kalman `minNumOfChannels=0`.
`DeleteNotSmoothed` confronta il numero di canali gia' smussati con questo
minimo: 0 >= 0 e' vero, quindi elimina tutti i canali con contatore <= 10.

Fixture: **8 misure valide diventano 0**. Questo non e' un problema di matrice
o di sensibilita' RF. Impedisce il normale aggiornamento del filtro GPS-only
e puo' intervenire quando il misto perde temporaneamente una costellazione.
Lo scenario richiede smoothing abilitato, come nella configurazione corrente.

### B. Regressione precedente: DOP geometrico obbligatorio per Kalman

`pvt/pvt.c:2274` tratta ogni FALSE di `DopEvaluation` come geometria PVT
invalida e resetta il metodo a LS. Il DOP geometrico non e' disponibile con
meno di quattro/cinque misure; Kalman puo' invece avere uno stato osservabile
tramite la covarianza predetta e aggiornarsi con meno misure.

E' una **regressione introdotta dal precedente controllo PVT**, non un difetto
che si puo' attribuire tutto al codice storico. La fixture conferma che tre
misure producono DOP non disponibile; il collegamento al reset e' verificato
nel chiamante. Non e' corretto ignorare genericamente gli errori DOP: vanno
distinti dati non finiti e assenza di DOP istantaneo, e va gestita esplicitamente
l'incertezza del filtro. Il caso A rende questo percorso particolarmente facile.

### C. Minimo satelliti non aggiornato dopo l'esclusione dell'ultima costellazione

`SelectChannelsForPvt` imposta il minimo a 5 per il misto. I filtri successivi
aggiornano i conteggi GPS/Galileo ma non ricalcolano il minimo.

Fixture: 4 GPS + 1 Galileo, l'ultimo Galileo sotto maschera. Rimangono 4 GPS
utilizzabili ma `ApplyElevationMask` restituisce FALSE perche' richiede ancora
5 misure. Il criterio va aggiornato dopo le esclusioni e associato al metodo
effettivamente eseguito. Il solver LS gia' sceglie correttamente 4/5 stati.

### D. Confronto TOW senza settimana al rollover

`pvt/pvt.c:614`, `VerifyTransmissionTime`: il primo controllo usa WN e TOW;
il confronto successivo minimo/massimo fra satelliti usa soltanto TOW.

Fixture: (WN=100, TOW=0.01 s) e (WN=99, TOW=604799.99 s). Sono separati da
20 ms, ma vengono scartati entrambi come incompatibili. L'aritmetica del range
ha un proprio wrap, che non puo' recuperare canali gia' esclusi prima.
Serve una rappresentazione normalizzata WN/TOW e una differenza temporale comune.
`SetReferenceRxTime` e `PredictRxTime` richiedono inoltre test dedicati per TOW
esattamente 604800 e per riferimenti negativi: diversi rami usano solo `>`.

### E. Ricostruzione WN0G sbagliata vicino ai multipli di 64 settimane

Il parser Galileo e `GetGstToGpsTimeOffset` ricostruiscono WN0G concatenando
i bit alti della settimana corrente ai sei bit trasmessi. Questo non sceglie
sempre il riferimento vicino: con settimana corrente 64 e riferimento 63
puo' ricostruire 127.

Fixture A1G=1e-13 s/s: offset calcolato -3.81024 us anziche' +0.06048 us.
Va scelto il candidato coerente col vincolo temporale dell'ICD, anche quando
il parser ritiene gia' completata la settimana. Il segno della conversione
GST-GPS in `AdjustMissingRxTime` e' invece coerente con la definizione ICD.
La disponibilita' della conversione viene ignorata in quel chiamante: offset
zero e' un fallback implicito, da rendere distinguibile da GGTO valido.

### F. Parametri Galileo usati anche per l'orbita GPS

`pvt/pvtConstants.h:99`, `ComputeSatsPositions`: GM unico 3.986004418e14
anziche' selezione per costellazione; anche F_CONST e' unico. Gli ICD GPS e
Galileo prescrivono valori diversi per le rispettive effemeridi broadcast.

Fixture orbitale a 3600 s da toe: differenza di posizione GPS **1.02335 m**
rispetto alle equazioni ICD; Galileo circa 4e-9 m. Sono errori sulle coordinate
del satellite, non metri di errore garantiti sul fix. L'impatto dipende da tk
e geometria e non spiega da solo la dispersione della registrazione.

### G. Velocita' non finita al perigeo

`pvt/pvt.c:916`: la derivata dell'anomalia vera contiene una divisione per
sin(tak), con numeratore anch'esso nullo quando E=0. La posizione e' regolare,
ma la velocita' diventa NaN. La fixture e' un'orbita valida all'epoca di perigeo.
La derivata va espressa senza questo rapporto singolare. Sul caso orbitale
ordinario la velocita' coincide invece con la differenza centrale delle
posizioni entro 3.6e-7 m/s: non e' sbagliata in ogni condizione.

### H. Rotazione terrestre Kalman dipendente dal bias di clock

`pvt/kalmanFilter.c:605` ricava il tempo di volo da pseudorangeCsm/c, che
contiene il bias ricevitore. LS usa invece la distanza geometrica/c.

Fixture con stesse posizioni, stessi Doppler e stesso contenuto geometrico,
aggiungendo 1e6 m sia alle pseudorange sia ai bias nominali: la posizione KF
cambia di **1.02541 m**. Una variazione del solo riferimento di clock non
dovrebbe modificare la geometria. E' un caso sintetico con bias ampio, non una
misura di quell'errore sul record. Serve un tempo di volo geometrico coerente
anche nel filtro, senza confondere ritardo fisico e bias della misura.

### I. GPS health: ignorati i cinque bit relativi al segnale

`ParseGpsSubframe1` valuta solo il bit 0x20 della health word. La fixture con
health 000010 (tutti i segnali dead) viene marcata SIGNAL_OK. La distinzione
fra salute dati LNAV e salute del segnale e' definita nella Tabella 20-VIII.
Occorre interpretare il servizio ricevuto, non semplicemente accettare ogni
health con MSB nullo. Il tracking potrebbe comunque respingere un segnale
assente; questo non rende corretto il criterio di selezione PVT.

## 3. Cosa e' risultato coerente, e cosa non e' stato certificato

**Parser e messaggi.** I vettori costruiti secondo i bit ICD confermano segno
e scala di M0 e sqrtA Galileo, toc/af0/af1/af2 Galileo e GPS. Il codice include
il controllo IODC basso/IODE per GPS e IODnav concorde per Galileo prima della
copia dal set temporaneo al set operativo. L'E1 I/NAV sceglie BGD(E1,E5b);
TGD/BGD sono sottratti alla correzione di clock e questa e' aggiunta al range,
con segno coerente. La relativita' e' presente nella correzione della misura.

Il controllo delle epoche GPS e Galileo rispetta la distinzione fra HOW GPS
e inizio pagina Galileo; non ho trovato ragioni per aggiungere indiscriminatamente
6 s o 2 s. I vettori non costituiscono un test completo di parity, FEC, CRC,
RedCED o ricostruzione dinamica dei contatori dopo una perdita di campioni.

**LS e integrazione.** Su misure sintetiche coerenti, il LS corrente recupera
la posizione entro 1e-5 m sia GPS sia misto con due bias diversi. La geometria
DOP e l'identita' PDOP^2=HDOP^2+VDOP^2 risultano coerenti. Il test riusa il
modello tropo NGene per generare le misure: verifica il solver e i bias,
non l'accuratezza fisica del modello atmosferico. Il contratto QR separato
passa; non autorizza a dichiarare corretta l'intera catena.

Restano sette iterazioni fisse senza verifica esplicita di convergenza,
assenza di pesi e di controllo statistico dei residui. Il residuo viene
calcolato per il log, ma non governa una procedura di esclusione degli outlier.
Le soglie di coordinate e GDOP non sostituiscono questo controllo.

**Atmosfera.** Il segno generale I/T e' coerente. Il LS pero' sovrascrive
la troposfera preparata dal manager con un altro modello, a quota fissata zero;
Kalman usa quella del manager con quota stimata. Il cambio di metodo cambia
quindi anche la misura corretta. I modelli andrebbero unificati prima di
attribuire differenze di quota al solo solver. Ionosfera broadcast/EGNOS/NeQuick
e' stata seguita nel flusso; non e' stata confrontata con un servizio ionosferico
indipendente o con dati di riferimento atmosferici.

**Velocita' e clock.** La correzione di clock satellitare viene applicata al
codice, ma la derivata del clock satellitare non viene applicata ai Doppler
nei due solver. Le velocita' satellite sono proiettate senza una trasformazione
completamente coerente con quella delle posizioni. Sono termini da completare
per velocita'/drift accurati, non prove della causa delle perdite RF.

**Kalman.** Il caso di innovazione nulla su utente in moto passa. La covarianza
in outage viene propagata e copiata nel banco multi correttamente nel caso
controllato. La forma Joseph e' presente: non occorre aggiungerla nuovamente.
L'azzeramento dello stato di correzione puo' essere coerente con un filtro
a errori reiniettati; non va scambiato automaticamente per perdita dello stato
fisico, conservato nei risultati nominali.

Restano l'inversa LDU dell'innovazione senza errore esplicito, covarianze
R divise per sin(elevazione) senza guardia locale e Q da validare su scenario.
Con maschera disabilitata, elevazioni nulle/negative non devono generare R
infinita o negativa. Con meno satelliti servono controlli della covarianza KF,
non il solo DOP istantaneo. Questi aspetti richiedono test longitudinali,
outage e transizioni GPS-only/Galileo-only/misto dopo le correzioni dei blocchi A-C.

**Integrita' dei dati.** URA/SISA non sono usati nei pesi. Lo stato health/DVS
operativo viene aggiornato assieme al set completo: l'applicazione immediata
dei nuovi avvisi va verificata. La settimana GPS dipende anche dalla correzione
di rollover configurata. Non e' stata eseguita una certificazione completa
di tutti gli stati speciali/integrity/UTC dell'ICD.

## 4. Relazione con l'instabilita' osservata e ordine degli interventi

Gli errori A-C possono togliere il fix e far alternare NAVIGATION/TRACKING
pur con correlatori ancora agganciati. Aggiungono reset dello smoothing e
ritardi di recupero. I bug di settimana/GGTO sono invece condizionali e non
sono automaticamente attivi nella registrazione del 27 ottobre.

Non e' stato dimostrato che questi problemi causino le perdite RF simultanee:
il record contiene gia' la discontinuita' descritta nel precedente report.
Non confondere una misura esclusa dal PVT con TRACKING_LOST. Nessuna soglia di
lock o di acquisizione e' stata allentata per migliorare artificialmente i conteggi.

Ordine di correzione proposto, mantenendo il metodo a ricezione comune:

1. Gestione misure/smoothing Kalman, disponibilita' DOP e ricalcolo del modello
   dopo le esclusioni; test di transizione e di outage senza reset immotivati.
2. Utilita' comune per differenze WN/TOW e ricostruzione delle settimane
   troncate; test a cavallo di settimana e blocco di 64 settimane.
3. Costanti per costellazione, derivata orbitale regolare e salute segnali.
4. Modello di range/rate e atmosfera condiviso fra LS e KF; soluzione SPD
   dell'innovazione con controllo degli errori, senza inversa esplicita.
5. Verifica convergenza/residui e pesi, poi replay LS/KF sugli stessi istanti
   e satelliti e confronto con ground truth. Non tarare i pesi per avvicinare
   semplicemente le coordinate alla posizione nota.

Cambiare ora al tempo comune di trasmissione o introdurre un diverso solver
matriciale non eliminerebbe questi difetti di timeline e gestione delle misure.

## 5. Correzioni implementate e verifiche finali

### Catena temporale, satelliti e osservabili

- Differenze WN/TOW in secondi senza timestamp assoluti in int o correzione
  della settimana dopo la moltiplicazione per c. I confronti fra satelliti
  includono WN. Normalizzazione delle epoche trasmesse e dei riferimenti
  ricevitore, inclusi TOW=604800 e TOW negativo. Il parser GPS aggiorna WN
  quando l'HOW attraversa la settimana prima della successiva subframe 1.
- WN0G ricostruita sul blocco piu' vicino, sia nel parser sia nel calcolo GGTO.
  Un riferimento completato fuori dall'intervallo previsto viene respinto.
  La conversione mancante non viene piu' presentata come tempo convertito valido:
  il primo LS puo' ancora stimare i due bias dalle origini convenzionali.
- Costanti GM/F distinte GPS/Galileo. Derivata dell'anomalia vera regolare
  anche al perigeo e derivate armoniche valutate sull'argomento non corretto.
  Le correzioni principali sono applicate anche al percorso almanacco.
- Derivata del clock satellitare broadcast inclusa nel modello Doppler di
  entrambi i solver. Posizioni ruotate con tempo di volo geometrico iterato,
  comune a LS, Kalman e DOP; velocita' satellitari espresse negli stessi assi.
- Health L1 C/A interpretata includendo i bit di stato del segnale, senza
  scartare L1 solo per un avviso riguardante una banda differente.

### LS e Kalman

LS conserva il QR Givens in double. Ora controlla anche la convergenza su
coordinate e bias (ultimo incremento <= 1e-4 m, limite di sette iterazioni).
Non restituisce successo solo perche' ha terminato il numero fisso di passi.
Usa la stessa famiglia di correzione troposferica Hopfield del manager/Kalman,
con quota stimata; non sovrascrive piu' il modello con una quota fissata a zero.

Kalman risolve l'innovazione SPD con **Cholesky e sostituzioni triangolari**,
senza formare un'inversa. La forma Joseph rimane attiva. Input non finiti,
varianze non positive e covarianze non definite positive producono fallimento
esplicito. La pesatura di elevazione ha un limite inferiore positivo; non puo'
generare varianze negative o infinite a elevazione nulla. Non sono stati
introdotti jitter o regolarizzazioni per nascondere una matrice non valida.

Sono stati eliminati dieci buffer dell'inversa/scratch LDU non piu' necessari:
circa **90.3 KiB** e relativi azzeramenti per 17 canali. Il solver aggiunge solo
scratch locale limitato per la verifica della covarianza. Non sono necessarie
Armadillo, BLAS, C++ o nuove dipendenze: le dimensioni sono 4/5 per LS e 8/9
stati per KF. QR e Cholesky sono implementazioni proprie; nessun codice
GNSS-SDR e' stato importato.

Quando entrambi i clock sono inizializzati, Kalman mantiene i nove stati anche
se una costellazione manca temporaneamente: conserva quindi le correlazioni
del banco completo. Il passo temporale viene ricavato dagli indici dei campioni
fra soluzioni, con il periodo configurato come inizializzazione.

### Misure, reset e disponibilita' del DOP

- Rimosso il reset dello smoothing a ogni epoca KF singola costellazione.
  Le nuove misure non smussate restano utilizzabili con la loro varianza alta,
  invece di essere eliminate tutte. La varianza si riduce dopo il transitorio
  del singolo canale, non in base a un flag globale invertito.
- Il minimo richiesto viene aggiornato a ogni esclusione di un canale.
- Un reset PVT soft preserva lo smoothing continuo. Hard reset o invalidazione
  del riferimento temporale lo cancellano; la nuova origine di una sola
  costellazione cancella solo il relativo storico. I bias cumulati cambiano
  solo per i clock effettivamente ribasati da osservazioni.
- Reset/inizializzazione invalidano correttamente anche il flag Galileo, al
  posto della vecchia assegnazione al valore numerico del bias. Clock e drift
  iniziali sono esplicitamente azzerati. I reset reinizializzano anche le
  covarianze KF, evitando di riusare una covarianza di un risultato respinto.
- `dopValid` distingue DOP geometrico disponibile e indisponibile. LS richiede
  il DOP; KF puo' usare la covarianza predetta con poche misure o senza misure.
  La previsione e' limitata a **100 m di sigma spaziale RMS formale**
  (`sqrt(trace(P_xyz))`), non e' una garanzia sull'errore reale.
- Il manager esegue e schedula la previsione senza misure, invece di ritornare
  continuamente sulla stessa epoca. Nessun bias cumulato viene incrementato
  in assenza di un nuovo riferimento osservato. Il DOP indisponibile appare
  come sconosciuto; NMEA lascia HDOP vuoto e, senza osservazioni, usa qualita'
  fix zero. Le epoche di sola previsione non vanno contate come nuovi fix GNSS.
- Il contatore PVT non aumenta piu' solo per il salvataggio di un risultato
  scartato. Restano i controlli di posizione/tempo e le soglie di lock originali.

### Test e risultati

Build Windows normale senza profiling: **10/10 CTest**. Il test matematico
include QR, soluzione SPD, matrici invalide e aritmetica delle settimane.
L'harness esteso compila 44 funzioni NGene correnti ed esegue **43/43 confronti**
in modalita' `-Verify`: include le nove discrepanze iniziali, passaggi misto /
GPS-only / Galileo-only / tre misure / misto, outage con incertezza crescente,
rumore invalido, preservazione smoothing, hard reset e clock rate satellitare.
Nessuno di questi test viene eseguito nel main operativo.

Replay del CI8 diagnostico completo, stesso riferimento WGS84 dell'utente:

| Configurazione | Soluzioni con misure | Sola previsione | LS / KF nel log | RMS H/U sulle soluzioni con misure [m] | Perdite RF |
| --- | ---: | ---: | --- | --- | ---: |
| LS misto 8/8/1 | 40 | 0 | 40 / 0 | 7.569 / 17.963 | 14 |
| KF misto 8/8/1 | 40 | 12 | 4 / 48 | 7.556 / 17.929 | 14 |
| LS GPS-only 8/0/0 | 63 | 0 | 63 / 0 | 2.720 / 7.549 | 8 |
| KF GPS-only 8/0/0 | 64 | 12 | 4 / 72 | 2.608 / 7.336 | 8 |
| Prima delle correzioni, configurato KF GPS-only | 43 | 0 | 43 / 0 | 4.291 / 7.793 | 8 |

Il vecchio eseguibile configurato per KF non produceva alcuna soluzione KF:
ricadeva su LS. Il conteggio totale 52/76 delle nuove configurazioni KF include
rispettivamente 12 previsioni; non rappresenta 52/76 nuovi fix con misure.

Sulle 43 epoche GPS comuni fra vecchia configurazione KF e nuova, tutte con
lo stesso numero di GPS, RMS H passa 4.291 -> 2.654 m e RMS U 7.793 -> 7.460 m.
Questo confronto riguarda l'insieme delle correzioni: non isola il merito del
solo algoritmo KF e non certifica uguali satelliti/pesi. Nel misto la geometria
debole e gli errori verticali rimangono importanti. Nessun parametro e' stato
tarato per avvicinare il risultato alla ground truth.

Replay SiGe IF di regressione: **23 soluzioni, zero perdite**, exit code 0.
Non si applica a quel file storico la ground truth della cattura HackRF.
Nei replay diagnostici non risultano hard restart o fallimenti PVT gravi.
Le perdite di tracking non sono state eliminate; i problemi USB/live e la
discontinuita' del file richiedono ancora una gestione esplicita della timeline.

Risultati completi, epoche comuni, hash e log di riferimento sono in
[pvt-fixes-20261028](development/profiling/pvt-fixes-20261028/README.md).
Tutti i replay finali LS/KF/SiGe usano l'eseguibile SHA-256
`6685EEED311091EE9A1EFABF34ECF6DDC3AB532B2BC4BD63BDFEBE684F9E69AB`.
La configurazione operativa resta quella dell'utente: `[PVT method] 0` per LS,
`1` per Kalman. Le prove modificano soltanto le copie isolate. Nessuna modifica
alla logica di acquisizione o al tracking e nessuna strumentazione di profiling
aggiuntiva nel main. Linux/live non sono stati eseguiti su questo host.
