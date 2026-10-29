# Transizioni tracking e PNT: fast fix del 29 ottobre 2026

## Risultato e limite principale

Implementato un mantenimento del tracking a breve termine, basato sugli NCO
del canale e su evidenze ripetute del ritorno del segnale. Il timeout e' un
limite dell'intero episodio, non il criterio per dichiarare recuperato il lock.
Tolto il riavvio RF provocato dai fallimenti PNT. Corrette inoltre le scale
binarie dei messaggi e il trattamento di Az nel wrapper NeQuick.

La prova controllata con 1.2 s di rumore sostituito al segnale passa da **9 a
2 perdite RF**, con 7 recuperi senza acquisizione. Con 400 ms recuperano tutti
i cinque canali entrati in mantenimento. Queste sono perturbazioni artificiali
con numero di campioni preservato: non provano la gestione di gap USB.

**La disponibilita' PNT peggiora in alcune prove.** Non si usano osservazioni
di mantenimento come misure. Si richiedono nuovo TOW decodificato e nuovo arco
di smoothing; il recupero PNT puo' seguire quello RF con diversi secondi di
ritardo. Il salto nel file originale resta irrecuperabile e, prima della
riacquisizione, viene atteso il limite del mantenimento. Non dichiarare
risolto il problema di continuita' o migliorata l'accuratezza.

## Analisi allegata e verifica nel codice

Considerato il PDF fornito dall'utente, `NGene Classic V2 — Analisi instabilita'
canali e PVT.pdf`, datato 29 ottobre. SHA256:
`6D78A9418A4D2171FE528498EB10E8CE7CB5284FF41E8D3D9D1661073064EFDF`.
Il documento descrive un audit statico e una simulazione, senza build/replay.
Le raccomandazioni sono state confrontate con lo stato corrente:

- Confermate l'asimmetria dei controlli PLL fra stati, il rientro attraverso
  la soglia alta e il rischio di ripetere le transizioni senza progresso.
  La simulazione del documento non e' una calibrazione del ricevitore:
  `InitAverageWindow` implementa una media esponenziale, non una finestra
  rettangolare di 100 campioni. Non adottate nuove soglie da quei numeri.
- Confermato il percorso da errori PNT a `HardRestartChannels`; eliminato.
- `NAVIGATION` continua a significare usato nell'ultima soluzione accettata.
  Non viene mantenuta artificialmente questa etichetta durante uno scarto.
- La cancellazione globale dello smoothing sui normali reset PNT soft era
  gia' stata corretta. Rimane il reset del canale quando perde il lock:
  conservarne ciecamente la fase durante un outage non e' una correzione sicura.
- Eigen era gia' integrato; non e' stato sostituito. I limiti su residui,
  innovazioni, ambiguita' e discontinuita' non dipendono dal cambio di libreria.

## Gestione implementata

La catena di acquisizione del singolo satellite, soglie comprese, e lo
scheduling fra costellazioni non hanno modifiche. Il main non contiene
strumentazione aggiuntiva. Mixer, correlatori e normali discriminatori/loop
mantengono l'aritmetica precedente.

1. Solo un canale gia' entrato in `TRACKING_LOCKED` puo' avere un predittore
   affidabile. Si salvano epoca campione e fasi NCO, insieme alle stime di
   frequenza gia' filtrate dal ricevitore. Un prompt inferiore ai vicini,
   oppure calato oltre 6 dB rispetto al precedente ancoraggio, non aggiorna
   il predittore mentre le metriche lente ricordano ancora il segnale.
   Questa qualificazione non dichiara una perdita RF. Un riferimento vecchio
   oltre 1 s o un contatore campioni retrogrado non avvia il mantenimento.
2. In `TRACKING_HOLDOVER` si propaga il predittore al campione corrente,
   correggendo anche la deriva accumulata prima della rilevazione del calo.
   I generatori continuano ad avanzare; i loop non inseguono il rumore.
   La frequenza del blocco completato (`*Old`) non viene riscritta.
3. Ogni 100 ms si controllano C/N0 e potenze early/prompt/late mediate su circa
   100 ms. Tre controlli consecutivi con prompt dominante e C/N0 sopra il
   cutoff piu' i 2 dB di isteresi esistenti consentono `TRACKING_RELOCK`.
   Qui il PLL puo' riallineare la fase: chiedergli di essere gia' allineato
   mentre e' congelato impediva il recupero anche con un segnale tornato forte.
4. Con i loop attivi servono tre controlli consecutivi con PLL e C/N0 validi,
   poi la verifica codice/portante del false-lock checker esistente. Cinque
   esiti tutti sul bin centrale consentono il rientro senza l'attesa destinata
   alla conferma di una correzione. Esiti diversi mantengono la sequenza
   storica di correzione e conferma. Il test separato copre entrambi i percorsi.
5. Le verifiche nei due stati del false-lock checker hanno cadenza 100 ms.
   Il mantenimento di un lock gia' dimostrato usa la soglia PLL bassa 0.4
   anche durante la verifica; il primo aggancio continua a richiedere 0.8.
   Le costanti numeriche non cambiano, ma questa e' una modifica esplicita
   delle decisioni del supervisore di tracking. PLL e C/N0 non possono piu'
   dichiararsi recuperati separatamente.
6. Il limite parte dal primo errore e comprende tutte le ricadute e conferme.
   Non viene riavviato passando fra stati. `[Tracking holdover ms]` e' opzionale:
   default 5000, intervallo 0..10000, 0 disabilita il mantenimento. Non e'
   un tempo minimo di attesa. Le configurazioni locali esistenti usano il
   default senza essere sovrascritte.

Durante l'episodio il canale non entra in PNT, non alimenta lo smoothing e non
decodifica bit dal segnale degradato. Si scartano parole/pagine/FEC parziali e
frammenti di autenticazione del parser, mantenendo effemeridi e WN gia' validati.
Il TOW viene invalidato fino a nuova decodifica; si scarta anche il primo bit
parziale al rientro. Non si dichiara preservata una misura di fase portante.

I controlli del tracking assestato vengono eseguiti prima di aggiornare i loop
e consegnare bit al parser. Il percorso iniziale conserva il precedente ordine.

## PNT, pseudorange e atmosfera

Dopo 10 fallimenti PNT gravi si reinizializza soltanto lo stimatore. Si invalida
il riferimento di posizione respinto, senza chiamare `HardRestartChannels` e
senza scartare le effemeridi. Eliminato `hardRestartMade` e il relativo bypass
dei controlli sul primo risultato successivo. Rimangono i controlli di geometria,
tempo, salti e valori non finiti. Un reset non rende accettabile un risultato
grossolanamente invalido, come verificato dai casi sintetici.

Le 35 scale negative `POW_TWO_Mxx` dei parser sono ora potenze di due esatte
in double (`0x1p-n`). In particolare 2^-14, 2^-15 e 2^-16 erano arrotondate
sensibilmente; altre differivano solo agli ultimi bit. I nuovi vettori verificano
segno e scala di alpha/beta GPS, Ai0/Ai1/Ai2, BGD e TOW Galileo. Nessuna modifica
alla struttura dei bit o alla logica di CRC/FEC.

Nel wrapper NeQuick mancavano Az=63.7 per la terna nulla e la limitazione a
[0,400] per le altre terne. Ora sono applicate prima dell'integrazione, secondo
le sezioni 2.5.4.4 e 3.3 del
[riferimento Galileo NeQuick G](https://www.gsc-europa.eu/sites/default/files/sites/all/files/Galileo_Ionospheric_Model.pdf).
Il test del wrapper controlla anche latitudine in radianti, quota in metri,
ora in ore e conversione STEC in ritardo L1. L'integratore e MODIP sono sostituiti
nel test per isolare questa interfaccia: **non e' la validazione completa dei
108 vettori ufficiali NeQuick**. Verificati inoltre il floor notturno Klobuchar,
il rollover giornaliero e segno, quota/elevazione del modello tropo storico.
Non aggiunti una calibrazione meteorologica, RAIM o un nuovo modello atmosferico.

Pseudorange a ricezione comune, rollover, clock rate, orbite, WLS/KF e due bias
restano coperti dall'harness precedente, esteso qui a **74/74 confronti**.
Non e' un audit esaustivo di ogni messaggio e condizione ionosferica.

## Verifiche e risultati

Build Windows normale con profiling OFF: **13/13 CTest**. I due nuovi contratti
usano le funzioni di produzione e input controllati. Verificano previsione delle
fasi, cadenza dei controlli, rientro con PLL sotto 0.8, ritorno iniziale in
quadratura, conferma dopo vera correzione, timeout anche nelle ricadute, input
non finiti, startup senza predittore, avanzamento del segnale e blocco dei parser.

| Prova | Perdite RF prima/dopo | Recuperi dopo | Output PNT prima/dopo |
| --- | ---: | ---: | ---: |
| Rumore artificiale 400 ms a 55 s, estratto 70 s | 0 / 0 | 5 su 5 episodi | 13 / 1 |
| Rumore artificiale 1200 ms a 55 s, estratto 70 s | 9 / 2 | 7 su 9 episodi | 1 / 1 |
| File originale misto 8/8/1, WLS | 13 / 13 | 0 su 12 episodi | 29 / 24 |
| File originale GPS 8/0/0, WLS | 8 / 8 | 0 su 8 episodi | 63 / 57 |
| File originale misto, KF finale | 13 | 0 su 12 episodi | 24 con misure + 12 previsioni |
| SiGe `native-ab.raw`, finale | 0 | nessun episodio | 23 |

Il tredicesimo fallimento misto riguarda un canale senza lock consolidato;
non viene trattenuto con un predittore non validato. Sul rumore da 400 ms i
recuperi RF richiedono 1.805..3.405 s dal rilevamento; sul caso da 1200 ms
2.305..3.405 s per i sette canali recuperati. Sono esiti del criterio e dei
filtri, non un periodo fisso imposto. Due Galileo non recuperano entro 5 s nel
caso lungo. Il controllo prima/dopo usa lo stesso file artificiale e configurazione.

Le minori disponibilita' PNT della tabella sono una regressione reale, dovuta
anche alla scelta prudente su TOW/smoothing. Il replay originale contiene il
salto noto a circa 77 s: prolungare il tracking non ricostruisce campioni mancanti.

Ground truth e metodo ENU sono quelli del logbook, senza offset d'antenna noto:

| WLS, tutte le epoche accettate | RMS H prima/dopo [m] | RMS U prima/dopo [m] |
| --- | ---: | ---: |
| Misto | 5.720 / 6.127 | 13.117 / 14.408 |
| GPS | 3.195 / 3.511 | 7.284 / 4.391 |

Gli insiemi temporali differiscono. Sulle 23 epoche miste e 40 GPS esattamente
comuni, con gli stessi conteggi satellitari, le coordinate stampate coincidono.
Questo non dimostra identita' delle osservabili a piena precisione o un vantaggio
metrico del mantenimento. I tempi CPU/wall delle prove sono variabili e non
costituiscono un benchmark di accelerazione.

Hash eseguibile finale:
`3770A2BAB3FB943D179B19116573637B51B23D1BDF513C0B564E397D9C09E665`.
Baseline locale: `build/tracking-before-holdover/ngene.exe`.
Patch di profiling ancora applicabile; nessuna differenza in acquisizione o main.
Risultati, hash di sorgenti/configurazioni, CSV, manifest e riferimenti ai log:
[tracking-review-20261029](development/profiling/tracking-review-20261029/results.json).

## Lavoro successivo e fase portante

Resta da verificare il comportamento Linux/live e dinamico. Servono eventi
espliciti di discontinuita' del frontend e una gestione degli archi di misura
che permetta di conservare il TOW/smoothing soltanto quando la continuita' e'
dimostrata. Il fast fix attuale privilegia il rigetto dei dati dubbi.

Non implementata una PVT a fase portante. L'integrale Doppler attuale e' usato
per lo smoothing; non e' una misura indipendente validata. Per produrre vere
misure servono accumulo dei cicli NCO, fase residua del correlatore, epoca
campione, gestione delle inversioni dei dati/mezzi cicli e degli archi dopo
slip/outage. TDCP puo' usare differenze fra epoche dello stesso arco per stimare
spostamenti; la posizione assoluta richiede anche gli stati di ambiguita' e il
modello statistico della combinazione codice/fase. Non aggiungere una seconda
pseudorange derivata dallo stesso Doppler contando due volte l'informazione.
