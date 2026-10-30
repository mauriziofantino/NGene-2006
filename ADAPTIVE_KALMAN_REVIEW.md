# Kalman adattativo - 30 ottobre 2026

Richiesta dell'utente: ridurre la dispersione osservata nel live Linux rispetto
al LMS, usando un unico filtro adattativo, senza scegliere statico/dinamico.
Questo intervento segue [KALMAN_REVIEW.md](KALMAN_REVIEW.md) e mantiene le
correzioni gia' verificate di origine temporale e covarianza integrata.

## Modello implementato

Gli stati restano posizione, velocita', bias GPS, deriva comune e, nel misto,
bias Galileo: 8 o 9 stati. La velocita' non viene mai forzata a zero. Lo stesso
modello rappresenta ricevitore fermo, velocita' costante e accelerazioni.
L'adattamento stima le incertezze Q e R dalle osservazioni.

1. Con almeno sei satelliti e geometria di rango sufficiente, un QR pesato
   separa le tre componenti di velocita' dalla deriva comune nei Doppler.
   La covarianza della velocita' deriva dalla geometria del QR e dalla
   dispersione residua. Non si usa la posizione nota dell'antenna.
2. Il residuo normalizzato del fit, diviso per N-4, stima il livello del rumore
   Doppler rispetto alle varianze iniziali. Una media esponenziale nel tempo
   stabilizza R; un'epoca piu' rumorosa la aumenta subito. Il modello mantiene
   la dipendenza dall'elevazione. Un fit grossolanamente incoerente non allena
   lo stimatore di movimento e viene fortemente depesato.
3. La potenza delle variazioni di velocita', meno la varianza prevista delle
   due stime Doppler, aggiorna la densita' del rumore di accelerazione.
   L'eccesso di potenza resta con segno prima della media: rettificare ogni
   campione avrebbe introdotto rumore di movimento artificiale da fermo.
4. Una forte innovazione di velocita' coerente con la geometria riapre Q
   nell'epoca corrente, prima dell'aggiornamento. Il filtro puo' cosi' reagire
   all'avvio e alla frenata dopo un lungo tratto tranquillo.
5. Con poche osservazioni, geometria insufficiente o outage, lo storico di
   velocita' viene invalidato e Q torna almeno alla densita' prudente iniziale.
   L'assenza di osservazioni non e' interpretata come immobilita'.

L'idea di usare Doppler per adattare il rumore di processo e di stimare il
rumore delle osservazioni e' presente anche nella letteratura GNSS, ad esempio
[Agarwal e O'Keefe, ION GNSS+ 2024](https://www.ion.org/publications/abstract.cfm?articleID=19884).
Il codice qui implementato e' originale; non si rivendica equivalenza con
l'algoritmo di quella pubblicazione o con i suoi risultati.

Limiti interni espliciti: memoria di 5 s, densita' di accelerazione compresa
fra 1e-4 e 1e3 m^2/s^3, scala minima del rumore Doppler pari a 0.01 volte la
varianza iniziale. Con la varianza nominale di 1 (m/s)^2, quest'ultimo limite
equivale a 0.1 m/s a elevazione zenitale. Il limite nominale dell'innovazione
usa 16.266, valore chi-quadro a tre dimensioni al 99.9%; poiche' la covarianza
e' stimata, questo non costituisce una garanzia d'integrita' o di falso allarme.
Questi limiti sono ingegneristici, non stimati dalla ground truth.

`[PVT method] 1` seleziona il Kalman adattativo. Non esiste un nuovo selettore
statico/dinamico. Il vecchio `[Static Receiver]` rimane per i controlli LS;
il KF verifica sempre la coerenza vettoriale con la velocita' precedente.
Un salto ortogonale non passa il controllo soltanto perche' ha la distanza
attesa. Il limite di salto non e' stato allargato.

## Verifiche

- 110/110 confronti dell'harness con funzioni estratte dal codice corrente.
- 14/14 CTest della build operativa Windows, profiling OFF.
- Casi dedicati: quiete, velocita' non nulla costante, avvio, frenata, salto
  del clock, singolo Doppler incoerente, outage, reset e controllo della
  direzione del movimento. La quiete riduce Q fino al limite inferiore;
  un salto comune del clock non viene classificato come accelerazione.
- Nessun cambiamento alle decisioni/soglie dell'acquisizione o alla precisione
  del DSP. L'algebra rimane Eigen in double con covarianza Joseph.

E' stata scartata una prima variante che adattava soltanto Q: lasciando la R
Doppler empirica fissa, alcuni tratti simulati peggioravano. La versione finale
adatta anche il livello del rumore Doppler dal fit ridondante.

## Traiettorie sintetiche accoppiate

Venti semi deterministici, stessa geometria e stessi disturbi per WLS, KF
precedente e KF adattativo. Il generatore usa otto satelliti, codice con
rumore correlato nel tempo (AR=0.875, sigma 3 m prima del fattore di elevazione),
Doppler con sigma 0.2 m/s prima del fattore di elevazione e deriva di 173 m/s.
La verita' e' nota soltanto al generatore e al calcolo degli errori.

Si eseguono realmente predizione del tempo, pseudorange, solver e aggiornamento
del riferimento. Traiettoria di 180 s: fermo, accelerazione a 2 m/s^2 fino a
12 m/s, velocita' costante, curva, frenata e nuovo arresto. Include quattro
epoche GPS-only e cinque epoche senza misure. Il rumore non e' una simulazione
integrale di multipath, tracking o campioni RF. I WLS dei due eseguibili sono
identici in tutte le venti prove.

Valori aggregati sui venti semi, KF precedente / adattativo:

| Fase | RMS posizione 3D [m] | RMS velocita' [m/s] |
| --- | ---: | ---: |
| Fermo, epoche 20-59 | 15.987 / 11.513 | 0.591 / 0.339 |
| Accelerazione | 12.243 / 10.823 | 0.664 / 0.525 |
| Velocita' costante est | 14.148 / 9.443 | 0.587 / 0.421 |
| Curva | 14.112 / 7.601 | 0.663 / 0.577 |
| Velocita' costante nord | 15.725 / 11.234 | 0.754 / 0.591 |
| Frenata | 13.407 / 11.744 | 0.663 / 0.521 |
| Nuovo arresto, epoche con misure | 12.596 / 9.782 | 0.563 / 0.390 |

Da fermo la dispersione centrata orizzontale passa da 2.500 a 1.443 m e quella
verticale da 10.150 a 5.835 m. Sono medie quadratiche fra prove e non precisioni
garantite del ricevitore. Il singolo seme puo' peggiorare in alcune fasi;
gli intervalli sulle differenze quadratiche accoppiate sono negli artefatti.

## Replay RF disponibile

Stesso CI8 diagnostico di ottobre, configurazione 8 GPS / 4 Galileo / 1 EGNOS,
CSM=8. Questo non e' un nuovo record della sessione Linux live dell'utente.
Il riferimento geografico e i limiti sono quelli del rapporto precedente.

| Caso | Misurati / previsioni | RMS H/U [m] | Dispersione centrata H/U [m] |
| --- | ---: | ---: | ---: |
| KF precedente misto | 24 / 12 | 4.943 / 4.696 | 4.346 / 1.552 |
| KF adattativo misto | 24 / 12 | 4.350 / 4.518 | 3.981 / 1.252 |
| KF precedente GPS | 58 / 11 | 3.611 / 4.906 | 2.054 / 2.069 |
| KF adattativo GPS | 58 / 11 | 3.723 / 4.619 | 1.729 / 2.227 |

Non tutte le metriche migliorano: nel GPS-only peggiorano l'RMS orizzontale e
la dispersione verticale, mentre si riducono dispersione orizzontale e RMS
verticale. Le previsioni non sono fix misurati. I numeri derivano da coordinate
testuali arrotondate. Nessun parametro e' stato scelto per avvicinare questi
punti al riferimento. I replay hanno ancora 10 perdite RF nel misto e 8 GPS;
non e' dimostrata una correzione dei problemi RF/live.

## Limiti residui

La R del codice rimane empirica e non rappresenta tutte le correlazioni del
carrier smoothing con Doppler e con le epoche precedenti. Il fit Doppler
ridondante non e' un sistema RAIM e non puo' distinguere ogni guasto coerente
dalla dinamica. Restano da validare i limiti su traiettorie RF reali, Linux
live, disturbi persistenti e geometrie diverse. Il filtro impara incertezze,
non una condizione fisica certa di immobilita'. Non e' aggiunta una soluzione
carrier-phase con ambiguita' risolte.

Artefatti e riproduzione:
[development/profiling/adaptive-kalman-20261030](development/profiling/adaptive-kalman-20261030/README.md).
