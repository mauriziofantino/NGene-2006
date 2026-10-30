# Revisione Kalman PVT - 30 ottobre 2026

**Revisione successiva:** il modello e' ora adattativo, senza selettore
statico/dinamico. Implementazione, verifiche e regressioni residue sono in
[ADAPTIVE_KALMAN_REVIEW.md](ADAPTIVE_KALMAN_REVIEW.md). Le sezioni sottostanti
descrivono il precedente modello con rumori fissi.

L'analisi riparte da `06696aec6eeb7a43d79c6e10eb287422bbc54eed`.
Sono stati riprodotti e corretti due difetti del modello: riutilizzo del bias
gia' rimosso dal riferimento temporale e covarianza di processo incoerente con
il passo temporale. E' corretto anche il cambio di origine dei clock non
osservati quando viene aggiornata la deriva comune.

Il miglioramento dell'RMS rispetto al riferimento e' dimostrato nel replay
misto disponibile; il caso solo GPS peggiora leggermente per questa metrica.
La dispersione rispetto al centro della nuvola e' valutata separatamente nella
sezione 5. Questo lavoro non certifica il comportamento
della sessione Linux segnalata dall'utente, della quale non sono disponibili
qui nuovi log o campioni. Rimangono limiti reali del modello delle misure.

## 1. Bias di clock applicato due volte fra epoche

`SetReferenceRxTime` aggiorna il riferimento usando:

```
t_ref = t_tx + pseudorangeRaw / c - bias_stimato / c
```

Il bias stimato e' quindi gia' sottratto dal tempo con cui verranno costruite
le nuove pseudorange. `PerformKalmanFilter` conservava tuttavia il vecchio
bias come nominale e lo sottraeva nuovamente dalle nuove osservazioni.
L'innovazione artificiale risultante poteva essere assorbita anche dalla
posizione, a causa delle correlazioni fra posizione e clock.

Riproduzione indipendente: ricevitore fermo, otto satelliti con azimut ed
elevazioni diversi, GPS/Galileo osservabili, codice e Doppler perfetti. Dopo
la rimozione di un bias comune di 100 m dalle misure:

| Controllo | Prima | Dopo |
| --- | ---: | ---: |
| Spostamento spurio della posizione | 47.832701 m | 2.64e-9 m |
| Controllo diagnostico con nominale esplicitamente azzerato | 3.37e-9 m | 2.64e-9 m |

I 100 m sono una perturbazione sintetica; non rappresentano una misura del
bias residuo del replay o della sessione Linux.

Ora il nominale e' azzerato soltanto se il riferimento della relativa
costellazione e' stato aggiornato all'ultima soluzione accettata. La sua
incertezza resta nella covarianza. Il bias di una costellazione assente viene
conservato. Quando cambia la deriva stimata, quel bias viene inoltre espresso
nella nuova origine usata da `PredictRxTime`, per l'intera eta' del riferimento:

```
b_non_osservato += delta_deriva_nominale * eta_riferimento
```

Due prove separate GPS assente / Galileo assente verificano questo passaggio
contro il cambiamento effettivo del tempo prodotto da `PredictRxTime`.
Prima veniva ignorato un cambio di origine di circa 1.985 m; ora la differenza
fra trasformazione del bias e del tempo e' circa 0.000215 m, entro la
risoluzione delle differenze temporali in double nella prova.

La convenzione storica di segno del drift e' conservata: il nominale viene
corretto con il segno opposto allo stato errore 7. Non va invertito il segno
di Phi solo perche' la correzione nominale del drift usa una sottrazione.

## 2. Covarianza di processo dipendente dalla partizione del tempo

Prima Q usava fattori diversi per posizione, velocita' e correlazione fra
le due: 1000, 100 e 1. Anche i termini del clock erano scalati in modo diverso,
con dt^3/2 sulla diagonale invece dell'integrale dt^3/3. Nel modello misto
mancava il termine comune fra i due bias, che integrano la stessa deriva.

Un processo continuo omogeneo deve rispettare:

```
Q(t1 + t2) = Phi(t2) Q(t1) Phi(t2)^T + Q(t2)
```

Con due passi da un secondo, il vecchio modello produceva 38.383333 m^2 di
rumore sulla posizione, contro 133.333333 m^2 per un solo passo da due secondi.
La differenza di 94.95 m^2 non dipende da osservazioni o ground truth.
Il nuovo modello supera l'identita' su tutte le componenti, sia a 8 sia a 9
stati.

Le densita' effettive gia' applicate a velocita' e drift sono conservate:
accelerazione 100*KF_SP = 5 m^2/s^3; rumore diretto del bias
100*KF_SF = 10 m^2/s; rumore della deriva 100*KF_SG = 1 m^2/s^3.
Non sono state ottimizzate sulla posizione nota. Le altre componenti sono
ricavate integrando queste stesse densita'. A dt=1 s:

| Componente Q | Prima | Dopo |
| --- | ---: | ---: |
| Posizione, per asse [m^2] | 16.666667 | 1.666667 |
| Velocita', per asse [(m/s)^2] | 5 | 5 |
| Posizione/velocita' [m^2/s] | 0.025 | 2.5 |
| Bias clock [m^2] | 10.5 | 10.333333 |
| Drift clock [(m/s)^2] | 1 | 1 |
| Bias/drift [m^2/s] | 0.005 | 0.5 |
| Bias GPS/bias Galileo [m^2] | 0 | 0.333333 |

La regola di predizione e il legame fra modello fisico, Phi e Q sono descritti
anche da [ESA Navipedia](https://gssc.esa.int/navipedia/index.php?title=Kalman_Filter).
Le formule e le prove specifiche qui riportate sono derivate per gli stati
del progetto; non sono codice copiato da un altro ricevitore.

## 3. Verifiche sintetiche

Lo stesso harness finale, con 52 funzioni estratte esattamente dai sorgenti:

- sorgente Kalman archiviato prima: 84 PASS / 7 MISMATCH;
- sorgente corretto: 91 PASS / 0 MISMATCH;
- build operativa Windows: 14/14 CTest, profiling OFF.

Il ciclo aggiunto usa realmente `PredictRxTime`, `ComputePseudoranges`,
`PerformKalmanFilter` e `SetReferenceRxTime`: misto, GPS, Galileo, misto,
assenza di misure e ritorno al misto, con drift di 173 m/s. L'errore massimo
di posizione passa da 96.007 m a 0.002823 m; l'errore finale di drift passa
da 1.810 m/s a -0.0000847 m/s. Il codice sintetico e' quantizzato dalle
differenze Tx/Rx a TOW=100000; le tolleranze sono 2 cm e 1 mm/s.

I vecchi controlli a innovazione nulla e con offset comune persistente non
verificavano il ciclo di ribasamento del tempo fra le epoche. Per questo
potevano passare pur lasciando attivo il difetto.

Le prove restano in `development/profiling/`; il main operativo non contiene
strumentazione aggiuntiva. Nessuna modifica ad acquisizione, tracking,
precisione dei correlatori o algebra Eigen/Joseph.

## 4. Replay prima/dopo

Input HackRF: `recordings/diagnosi/hackrf-l1-ci8.raw`, 1,964,112,000 byte.
Stessa cattura con discontinuita' nota, stesso riferimento WGS84 fornito
dall'utente: lat 45.06538716666666, lon 7.659323472222223, h 311.9349 m.
Offset rispetto al riferimento antenna non disponibile.

Configurazione locale corrente: 8 GPS / 4 Galileo / 1 EGNOS, CSM=8, WLS
abilitato per l'inizializzazione, 1 Hz. Non confondere questi risultati con
i vecchi replay 8/8/1. Gli override del solver sono soltanto nelle copie dei
replay; il `PVT method` della configurazione locale resta 0, cioe' LS.

| Replay | Output con misure | Sola previsione | RMS H [m] | RMS U [m] | Perdite RF |
| --- | ---: | ---: | ---: | ---: | ---: |
| WLS misto | 24 | 0 | 7.175 | 7.968 | 10 |
| KF misto prima | 24 | 12 | 5.555 | 5.426 | 10 |
| KF misto corretto | 24 | 12 | 4.943 | 4.696 | 10 |
| KF GPS prima | 58 | 11 | 3.547 | 4.896 | 8 |
| KF GPS corretto | 58 | 11 | 3.611 | 4.906 | 8 |
| KF SiGe prima | 23 | 0 | non valutato | non valutato | 0 |
| KF SiGe corretto | 23 | 0 | non valutato | non valutato | 0 |

Le 24 epoche miste e le 58 GPS prima/dopo coincidono per RxMs e conteggi di
costellazione. I numeri includono l'inizializzazione LS. Limitando il confronto
alle epoche effettivamente KF in entrambi i run:

| Confronto | Epoche KF comuni | RMS H prima/dopo [m] | RMS U prima/dopo [m] |
| --- | ---: | ---: | ---: |
| Misto | 22 | 5.538 / 4.863 | 5.508 / 4.721 |
| GPS | 54 | 3.372 / 3.444 | 4.777 / 4.788 |

Le previsioni non sono nuovi fix GNSS. Le coordinate sono ricavate dal testo
arrotondato; le cifre dei risultati non indicano accuratezza centimetrica.
Stessi campioni RF e conteggi non provano osservabili identiche: la retroazione
del clock puo' modificare tempi e smoothing. Non attribuire l'intero risultato
a uno solo dei due interventi. Il riferimento HackRF non viene applicato a
SiGe. Nessuna misura di miglioramento universale o di CPU Linux.

## 5. Chiarimento dell'utente: dispersione live su Linux

L'utente precisa che il problema osservato e' una nuvola di punti Kalman molto
piu' dispersa del LMS durante una sessione Linux live. Questo dato non equivale
al solo RMS rispetto alla posizione di riferimento: uno spostamento del centro
e un allargamento della nuvola sono effetti differenti.

Il confronto aggiunto sottrae la media di ciascuna serie sulle stesse epoche:
dispersione orizzontale = sqrt(var(E) + var(N)), verticale = deviazione standard
di U. Sono escluse le previsioni senza osservazioni.

| Dati del replay | Epoche comuni | Dispersione H prima/dopo [m] | Dispersione U prima/dopo [m] |
| --- | ---: | ---: | ---: |
| KF misto, vecchio/corretto | 24 | 4.694 / 4.346 | 2.673 / 1.552 |
| KF GPS, vecchio/corretto | 58 | 2.163 / 2.054 | 2.066 / 2.069 |
| WLS misto / KF misto corretto | 14 | 7.331 / 4.422 | 5.426 / 1.646 |

Per il confronto WLS/KF, soltanto 14 delle rispettive 24 epoche coincidono
esattamente per RxMs e conteggi di costellazione. Le altre non sono usate in
questa riga. Il piccolo peggioramento dell'RMS GPS della sezione 4 accompagna
quindi una minore dispersione orizzontale e un centro leggermente piu' distante
dal riferimento. Questi concetti non vanno confusi.

Il replay disponibile non riproduce la maggiore dispersione Kalman rispetto
al LMS segnalata nel live. Non dimostra che le correzioni abbiano risolto quella
sessione. Non sono qui disponibili il suo log KF e quello LMS; il vecchio log
live in `recordings/diagnosi` non costituisce quel confronto.

**Modello attuale:** velocita' costante, con accelerazione aleatoria. La posizione
nominale viene propagata come p_next = p + v*dt. `Static Receiver=1` modifica
soltanto il controllo dei salti in `CheckPvtResultsGoodness`; non entra nella
costruzione di Phi o Q. Il filtro non adotta quindi un modello a posizione
costante quando quel parametro e' attivo. A 1 s la densita' conservata aggiunge
5 (m/s)^2 di varianza per asse alla velocita'. Una velocita' stimata rumorosa
puo' trasferirsi alla posizione tramite la predizione e le correlazioni del KF.
E' un meccanismo plausibile per il live, non una causa misurata su quei dati.

Un modello statico richiede una scelta esplicita degli stati, dell'incertezza
di posizione e del trattamento di Doppler/velocita'; non consiste soltanto nel
ridurre una costante finche' i punti diventano vicini. Va distinto dal modello
dinamico e verificato anche durante il ritorno delle misure dopo un'interruzione.
Nessun nuovo modello statico e' stato introdotto in questa analisi.

## 6. Limiti ancora aperti del Kalman

1. **Modello delle misure R.** Varianze empiriche fisse e salto di fattore 100
   al termine del transitorio; non descrivono completamente C/N0, multipath,
   URA/SISA e atmosfera residua. Il codice smussato incorpora Doppler e storia
   delle epoche precedenti: la R diagonale non rappresenta queste correlazioni.
   La ricorsione Hatch e' documentata anche nelle
   [osservabili GNSS-SDR](https://gnss-sdr.org/docs/sp-blocks/observables/);
   il problema specifico delle correlazioni e' dedotto dal codice NGene.
2. **Innovazioni.** Non c'e' ancora un gate statistico normalizzato per misura
   o un test globale NIS con esclusione e ricalcolo. Joseph e Cholesky validano
   la matematica della covarianza, ma non respingono da soli una pseudorange
   fisicamente errata. Il limite formale di 100 m non e' una garanzia d'integrita'.
3. **Dinamica.** `Static Receiver` controlla i salti; non seleziona un modello
   Kalman statico. La densita' d'accelerazione conservata puo' essere troppo
   permissiva per un'antenna ferma. Va valutata con innovazioni e dati dinamici,
   senza stringerla per far convergere il grafico alla ground truth.
4. **Inizializzazione.** P iniziale resta una diagonale fissa: non deriva dalla
   geometria e dalla covarianza della soluzione WLS iniziale.
5. **Dati.** Servono log o cattura della sessione Linux problematica per
   verificare che i difetti riprodotti spieghino anche quel comportamento.

Priorita' successiva: innovazioni e coerenza osservabili/R su epoche e satelliti
identificati, poi esclusione statistica e confronto statico/dinamico. Le sole
modifiche a Q non risolvono multipath, gap RF o dati incoerenti.

Artefatti, hash, configurazioni e comandi sono in
[development/profiling/kalman-review-20261030](development/profiling/kalman-review-20261030/README.md).
