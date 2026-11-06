# Cadute sincrone: input, ring e finestre dei canali

Verifica iniziata il 5 novembre, completata il 6 novembre 2026.

Il test `check-input-channel-timeline.exe` compila il ring e il decoder SiGe
di produzione e tre funzioni estratte esattamente da `channelManager.c`:
`GetSamplePointersForChannel`, `GetNumberOfUsedSamples` e
`CleanBufferOffsetForAllChannels`. Un lettore indipendente confronta il
contenuto disponibile e le finestre di otto canali a offset differenti.
Sono controllati ordine, indici, contabilita' del ring e conservazione
dell'epoca campione nel riposizionamento degli offset.

Risultato su tutto il file, in entrambe le modalita' (letture intere e
frammentate, incluse letture vuote, byte dispari e attraversamenti del bordo):

| Record | Byte originali verificati per modalita' | Esito |
| --- | ---: | --- |
| SiGe native-ab.raw | 794615808 | PASS |
| HackRF diagnosi/hackrf-l1-ci8.raw | 1964112000 | PASS |

Il contratto USB ora verifica anche completamenti fuori ordine, letture
parziali 7+9 byte e attraversamento della coda intermedia. Passano inoltre
i precedenti casi concorrenti di backpressure/shutdown e timeout/restart.
Build normale Windows: 14/14 CTest, profiling OFF.

Queste prove non riproducono una corruzione dei percorsi verificati. Non
escludono errori driver/hardware live, concorrenza non esercitata o problemi
nei loop DSP. Il main e gli algoritmi GNSS operativi non sono stati modificati.

## Prova del limite di recupero a tre secondi: ritirata

Stesso eseguibile `2B8A4DFDCE7D6A4967068D9FF75C7AC43FB46A2DBF88F802D14CC025DC9AFAB3`,
stessa configurazione 8 GPS / 4 Galileo / 1 EGNOS, override isolato 3000/5000 ms:

| RF | Limite | Output PNT | Ingressi holdover | Recuperi | Perdite RF |
| --- | ---: | ---: | ---: | ---: | ---: |
| HackRF originale | 5000 ms | 24 | 10 | 0 | 10 |
| HackRF originale | 3000 ms | 24 | 10 | 0 | 10 |
| Calo artificiale 400 ms | 5000 ms | 1 | 6 | 5 | 1 |
| Calo artificiale 400 ms | 3000 ms | 1 | 6 | 4 | 2 |
| SiGe originale | 3000 ms | 23 | 0 | 0 | 0 |

Tre secondi superano il contratto sintetico di conferma dopo correzione, ma
falliscono un recupero RF reale che passa con cinque. La modifica provvisoria
di default/template/configurazione locale e' stata ritirata. Non dichiarare
il timeout piu' corto una soluzione dell'instabilita'. Rimane disponibile
`profile-replay.ps1 -TrackingHoldoverMs` per confronti isolati.
I conteggi non dimostrano accuratezza metrica. Il lungo tempo wall di una
prova attraversa una sospensione dell'host: non usarlo come benchmark.

## Limite confermato da affrontare

Un restart USB SiGe incrementa la generazione dello stream e svuota la coda;
il ring gestisce l'allineamento I/Q, ma non propaga la discontinuita' alla
timeline dei canali/PVT. La perdita ignota di campioni rende insufficienti le
vecchie fasi NCO. L'holdover attuale non cerca un nuovo ritardo codice.
Questo e' un limite del recupero, non la prova della causa dell'episodio
live dell'utente. Il record SiGe disponibile non riproduce cadute RF.
Serve il log o un record dell'episodio per distinguere restart, perdita RF
o errore della catena comune. Nessuna prova SiGe hardware eseguita: assente.

## Ripetizione

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File development/profiling/build-diagnostic-tools.ps1
& build/diagnostic-tools/check-input-channel-timeline.exe recordings/native-ab.raw --sige
& build/diagnostic-tools/check-input-channel-timeline.exe recordings/diagnosi/hackrf-l1-ci8.raw
```

Log locali: `build/input-timeline-sige.log`, `build/input-timeline-hackrf.log`,
`build/input-timeline-short-recovery-build.log` e build finale
`build/input-timeline-restored-build.log`. Manifest dell'estrazione:
`build/diagnostic-tools/channel-source.json`. Riferimenti dei replay e
conteggi in `results.json`.
