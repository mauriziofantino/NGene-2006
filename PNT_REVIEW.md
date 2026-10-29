# Revisione PNT, osservabili ed EGNOS — 29 ottobre 2026

## Risultato

La navigazione viene chiamata PNT nei messaggi operativi. I percorsi `pvt/`,
`PVT.conf`, i nomi C e le chiavi di configurazione esistenti rimangono compatibili.
La scelta di **Eigen** e' stata confermata dall'utente dopo il confronto con
Armadillo. Non si attribuisce alla libreria un miglioramento automatico della
precisione del fix.

Sono implementati: algebra lineare di libreria in double; QR con pivoting;
LS pesato selezionabile; passo Kalman di libreria con Joseph; correzioni dello
smoothing; codici SBAS 121/123; protezione dalle correzioni SBAS in test (MT0).
Non sono cambiate le decisioni coarse/refinement/conferma M-su-N, le soglie
di acquisizione o i loop di tracking. Lo scheduling Galileo resta presente.

## 1. Matematica e algoritmi

`pvt/pntNumerics.cpp` e' il confine C/C++: usa Eigen per prodotti, somme,
trasposte, proiezioni di covarianza, QR, Cholesky e soluzioni triangolari.
Eliminati `utility/matrices.c`, `smallLeastSquares.h` e `smallSpd.h`, incluse
le vecchie implementazioni di inversa LDU. I nomi `Matrix*` rimasti sono
adattatori di memoria per il codice C, senza algoritmi matriciali locali.
La costruzione del modello GNSS e l'assemblaggio degli stati restano nel C.

- LS: `ColPivHouseholderQR` in double, controllo del rango e dei valori finiti,
  nessuna formazione delle equazioni normali. DOP ricavato da R e dalla
  permutazione del QR, con geometria non pesata alla posizione finale.
- WLS: `[PNT weighted LS] 1` (default se assente) sbianca codice e Doppler con
  la varianza per canale e il fattore di elevazione gia' usati da Kalman.
  `0` consente il confronto LS non pesato sullo stesso eseguibile.
  Il primo passo, quando l'elevazione non e' ancora definita, e' non pesato.
  Non sono stati introdotti pesi derivati dalla ground truth.
- Kalman: predizione e aggiornamento con operazioni Eigen; soluzione
  dell'innovazione con Cholesky, Joseph per la covarianza. Un fallimento
  numerico non pubblica uno stato/covarianza parzialmente aggiornato.
  Rumore di processo semidefinito positivo ammesso, rumori invalidi respinti.
  Restano i due clock nel misto e il limite d'incertezza delle previsioni.
- Rimosso lo scratch obsoleto di LS e KF e il suo azzeramento a ogni epoca.
  La conversione geodetica della posizione in LS viene fatta una volta per
  iterazione anziche' per satellite. Eigen gestisce temporanei a frequenza PNT;
  non si rivendica assenza di allocazioni dinamiche.

Armadillo era una scelta valida: offre gli stessi strumenti necessari,
usando BLAS/LAPACK per le fattorizzazioni. Eigen riduce le dipendenze da
compilare/distribuire e si adatta alle matrici piccole del progetto (4/5
incognite LS, 8/9 stati KF). Non e' stato effettuato un benchmark comparativo
Armadillo/Eigen, ne' e' dimostrata superiorita' di accuratezza dell'una.
Riferimenti: [Armadillo](https://arma.sourceforge.net/faq.html),
[QR Eigen](https://eigen.tuxfamily.org/dox/classEigen_1_1ColPivHouseholderQR.html).

CMake usa Eigen di sistema >=3.4 o il sorgente upstream 3.4.0 fissato da SHA256.
Le licenze sono in `libraries/eigen-*`. La build numerica non usa fast-math
ne' parallelismo Eigen. Linux richiede anche un compilatore C++17;
Windows usa clang-cl gia' presente. Linux/live non eseguiti su questo host.

## 2. Pseudorange, ICD e fase

Riletti i punti pertinenti degli ICD forniti, nelle estrazioni locali
`build/icd-audit/IS-GPS-200N.txt` e `Galileo_OS_SIS_ICD_v2.2.txt`.
Documenti/hash e verifiche precedenti sono nel
[rapporto pseudorange](PSEUDORANGE_PVT_AUDIT.md).

Il modello a ricezione comune resta coerente: il termine `-bufferOffset/fs`
riporta i canali alla stessa epoca del ring; si usa c*(Rx-Tx) con settimane
normalizzate e si aggiunge la correzione del clock satellite, con TGD/BGD e
relativita'. Le origini separate GPS/Galileo sono assorbite dai due bias.
Non si e' passati a un tempo comune di trasmissione. Restano confermati dai
test sintetici rollover, clock rate, orbite e soluzione con due bias.

Nuove correzioni dimostrate nello smoothing:

1. Il manager lo chiama **dopo** il tracking, quando `dopplerShift` e' gia'
   quello del prossimo blocco. L'integrale ora usa `dopplerShiftOld`, cioe'
   la frequenza del blocco realmente elaborato; anche il riallineamento
   all'epoca comune usa quella frequenza.
2. L'avvio Hatch usa 1/min(numero epoche, finestra), anziche' dare da subito
   al primo campione rumoroso il peso storico di una finestra gia' piena.
3. Codice corretto e Doppler grezzo non erano nello stesso dominio: le
   variazioni delle correzioni clock/SBAS finivano filtrate. La previsione
   smussata ora aggiunge la differenza delle correzioni tra le due epoche,
   preservando immediatamente un aggiornamento broadcast. Il reset del canale
   cancella anche questo storico. Il ribasamento del clock resta coerente.

**Non e' stata aggiunta una soluzione di posizione a fase portante assoluta.**
Il dato chiamato carrier smoothing e' un integrale del Doppler, con correzione
del drift ricevitore, non una nuova osservazione indipendente di fase validata.
`carrierPhase` e' la fase NCO modulo 2 pi greco. Per una soluzione carrier-based
servono accumulo coerente dei cicli, epoche delle correlazioni, gestione
dell'ambiguita' di mezzo ciclo/dati, cycle slip e stato di ambiguita' per arco,
oltre al modello ionosferico e alle correlazioni con il codice smussato.
Usare ora questo dato come un'altra pseudorange conterebbe due volte parte
dell'informazione e potrebbe sottostimare l'incertezza. Il nuovo record pulito
servira' a validare una futura osservabile di fase/TDCP.

Il WLS usa ancora il modello empirico di varianza gia' presente nel KF:
non e' una stima completa di URA/SISA, multipath e rumore atmosferico residuo.
Non aggiunge RAIM, esclusione statistica degli outlier o una garanzia di integrita'.

## 3. EGNOS: causa verificata e correzione

La lista precedente era 120,124,126,136: **mancavano 121 e 123**. Il servizio
ufficiale consultato indica 121/136 operativi e 123 in test, con snapshot
aggiornato il 13 ottobre 2026; non va confuso con una verifica live del 29.
[Stato EGNOS](https://egnos.gsc-europa.eu/egnos-system/realtime).

`egnosPrns.h` centralizza mapping e generazione G1/G2 secondo IS-GPS-200N,
tabella 6-I. Per i quattro PRN storici tutti i 8184 byte BPSK/sign-mask
coincidono con il file originale. Per 121/123 verificati anche i primi dieci
chip ICD (ottale 0536/1545). Non vengono piu' usati codici finti a zero quando
manca un file EGNOS. La ricerca automatica prova 121,136,123 e poi i tre PRN
storici, senza modificare le fasi/decisioni del singolo tentativo.

Il file FFT storico contiene quattro slot EGNOS prima di Galileo: sono
consumati esattamente quattro record e le FFT dei nuovi PRN sono generate a
startup al sample rate effettivo. Questo evita di leggere una FFT Galileo
come EGNOS o di spostare tutto il resto del file. Il formato salvato resta
quello storico. Il replay SiGe esercita anche il percorso delle FFT precaricate.

Sul record diagnostico esistente, EGNOS123 entra ora in tracking al campione
24706671, FLL locked a 37800744, PLL locked a 39903979. Perde il lock vicino
alla discontinuita' nota (640676392) e riparte a 670072237. E' evidenza di
acquisizione/tracking reali, non ancora di validita' delle correzioni SBAS.

MT0 in precedenza veniva soltanto registrato. Ora invalida le correzioni
gia' ricevute e impedisce di accettarne altre per un minuto dall'ultimo MT0,
anche attraverso il rollover settimanale. Il segnale puo' restare tracciato.
La banca condivisa e' coerente con il singolo canale EGNOS supportato; una
futura ricezione multi-GEO richiede banche separate per sorgente.
Il test sintetico verifica l'invalidazione e la finestra; non si dichiara
completata una certificazione del parser/integrita' SBAS.
[Descrizione ESA dei messaggi](https://gssc.esa.int/navipedia/index.php/The_EGNOS_SBAS_Message_Format_Explained).

## 4. Risultati dei replay

RF diagnostico precedente, non il nuovo record atteso. Stesso riferimento
WGS84 dell'utente, senza offset d'antenna noto. Tutti i seguenti replay usano
lo stesso binario di confronto; hash e percorsi sono negli artefatti.

| Modalita' | Soluzioni con misure | Previsioni | RMS H [m] | RMS U [m] | Perdite RF |
| --- | ---: | ---: | ---: | ---: | ---: |
| LS misto non pesato | 29 | 0 | 6.137 | 13.841 | 13 |
| LS misto pesato | 29 | 0 | 5.720 | 13.117 | 13 |
| Kalman misto, inizializzazione WLS | 29 | 12 | 6.274 | 16.361 | 13 |
| LS GPS non pesato | 63 | 0 | 3.395 | 7.068 | 8 |
| LS GPS pesato | 63 | 0 | 3.195 | 7.284 | 8 |

Il confronto WLS/LS usa lo stesso codice e la stessa registrazione; le epoche
e i conteggi per costellazione sono confrontati negli artefatti. La pesatura
aiuta qui l'orizzontale ma non sempre la verticale. Kalman non e' universalmente
migliore del WLS: nessuna taratura di Q/R sulla ground truth e' stata eseguita.

Rispetto al binario della sessione precedente, GPS LS era 2.720/7.549 m H/U:
il nuovo insieme di modifiche **non migliora tutte le metriche**. Nel misto
si passa da 40 a 29 soluzioni con misure e cambia la storia dei satelliti;
EGNOS prima occupava una ricerca continua e ora entra in tracking, influendo
sul budget adattativo dello scheduler. Non attribuire al solo solver ne'
questo conteggio ne' la differenza di CPU. Le 13 perdite includono EGNOS;
le 14 precedenti riguardavano un insieme diverso di canali tracciati.

SiGe IF di regressione: 23 soluzioni, nessuna perdita di tracking. Non si
applica a questa registrazione la ground truth HackRF. Nessun hard restart
nei replay. Restano da verificare USB/live, accuratezza sul nuovo RF non
saturo, integrita' temporale e comportamento Linux.

## 5. Inefficienze e priorita'

I costi misurati nelle sessioni precedenti restano dominati da ricerca e DSP,
non dalla PNT a 1 Hz. La rimozione dello scratch e delle conversioni ripetute
riduce lavoro inutile, ma non e' un benchmark di speedup complessivo.
L'acquisizione EGNOS continua su PRN non presenti era lavoro improduttivo:
aggiornare la copertura PRN permette ora lavoro utile di tracking.

Prima di introdurre un pool multicore servono scratch privati, finestre di
campioni possedute e commit ordinato degli aggiornamenti dei canali: i
registri/scratch SIMD condivisi non consentono un semplice parallel-for.
Non sono state introdotte approssimazioni numeriche per alleggerire il DSP,
ne' strumentazioni aggiuntive nel main. Le misure restano in
`development/profiling/`. Build finale con profiling OFF.

Test e risultati ripetibili in
[development/profiling/pnt-review-20261029](development/profiling/pnt-review-20261029/README.md).
