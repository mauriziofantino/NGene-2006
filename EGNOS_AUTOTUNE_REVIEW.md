# EGNOS e autotune HackRF - 30 ottobre 2026

## EGNOS: ricezione, dati e contributo al PVT

L'utente segnala pochi agganci EGNOS e nessun passaggio a NAVIGATION.
Nel codice erano presenti due cause distinte:

- `ReadyForPvt` esclude i GEO EGNOS dal sistema di misure di ranging. Ncurses
  usava soltanto `usedForPVT` per mostrare NAVIGATION, quindi un GEO rimaneva
  sempre TRACKING anche quando il suo messaggio SBAS veniva decodificato.
- Il tracking alimentava il decoder EGNOS soltanto con tempo GPS gia'
  disponibile. Sincronizzazione e CRC venivano quindi ritardati fino al tempo
  del ricevitore, pur non richiedendolo.

Ora simboli, sincronizzazione e CRC vengono elaborati anche prima del tempo
GPS. Le correzioni prive di timestamp affidabile restano escluse. Un MT0
ricevuto senza tempo invalida comunque la banca; quando il tempo torna
disponibile parte una finestra prudente completa di sessanta secondi. Non
si perde l'avviso di servizio in test durante questa transizione.

Ncurses distingue:

| Stato | Significato |
| --- | --- |
| TRACKING | Nessun frame recente validato, oppure lock non consolidato |
| SBAS NO TIME | Frame con CRC valido, tempo GPS ancora indisponibile |
| SBAS DATA | Frame recenti con CRC valido e tempo GPS disponibile |
| SBAS TEST | Servizio bloccato da MT0; correzioni escluse |

La colonna messaggio mostra `CRC OK`, che non significa autenticazione
crittografica. Un frame vecchio di oltre tre secondi non viene presentato
come dato corrente. Il reset dei frammenti e un CRC fallito invalidano lo
stato. Non si attribuisce falsamente il GEO alle pseudorange del fix e
`SBAS DATA` non garantisce che tutte le correzioni necessarie siano disponibili
o effettivamente applicate.

La pagina ufficiale EGNOS, snapshot consultato il 30 ottobre con aggiornamento
29 ottobre 22:59:57 UTC, indica PRN 121 e 136 operativi, PRN 123 in test MT0/2:
[EGNOS realtime](https://egnos.gsc-europa.eu/egnos-system/realtime).
La ricerca corrente gia' prova 121, 136 e poi 123, mantenendo i PRN storici
per replay. Nel record diagnostico si aggancia 123; questo non dimostra che
121/136 siano ricevibili con la stessa antenna o che il live dell'utente
abbia gli stessi segnali. Non sono state alterate soglie, conferma M-su-N
o fasi dell'acquisizione per ottenere piu' agganci.

## Autotune HackRF: configurazione e dashboard

Il template gia' abilitava l'autotune, mentre il valore predefinito del parser
era disabilitato. La configurazione locale preesistente non aveva il tag:
il percorso funzionava nei test con autotune esplicito, ma restava spento
con quel file. Ora il default e' coerente col template e la configurazione
locale esplicita le voci:

```ini
[Auto tune gains]
1

[Auto tune target RMS]
28
```

Il file e' `configuration/HackRF.conf`, accanto all'eseguibile. `0` disabilita
la taratura e usa i guadagni LNA/VGA del file; `1` la esegue una volta prima
di avviare la timeline operativa. Un `0` esplicito viene rispettato. Il target
e' la deviazione standard di ciascuna componente ADC dopo la sottrazione del
DC misurato, nell'intervallo ammesso 8-40 conteggi. Non e' un target di C/N0.

Ncurses conserva lo stato sulla riga 3, prima della tabella PVT: pending,
calibrazione, tentativo corrente e guadagni finali, oppure disabled/bounded/
failed. I messaggi successivi del ricevitore non cancellano questo risultato;
il resize ridisegna lo stato conservato. In replay la riga spiega che non
viene effettuata una taratura hardware sui campioni gia' registrati.

E' corretto anche un caso limite: una sola finestra accettata al dodicesimo
tentativo non viene piu' dichiarata convergenza. Servono due finestre
consecutive accettate. Il raggiungimento del limite senza conferma e' una
condizione distinta, visibile e registrata nel log. Gli errori di calibrazione
o aggiornamento dei guadagni vengono segnalati esplicitamente.

Il controllo dei guadagni continua a usare DC, sigma e clipping ADC misurati;
non cambia amplificatore RF o alimentazione antenna. I campioni di calibrazione
restano separati dai campioni operativi. I guadagni vengono congelati prima
del tracking e una ripartenza dello stream non ritara un arco gia' attivo.
Questo e' autotune iniziale, non AGC durante la navigazione.

E' corretta inoltre la stampa ncurses con formato `%s`, per visualizzare
letteralmente eventuali percentuali nei messaggi.

## Verifiche e limiti

I contratti della build verificano: default abilitato e disabilitazione
esplicita, messaggi operativi di taratura, due conferme di convergenza al
limite dei dodici tentativi, isolamento dei campioni, ripartenza senza nuova
taratura, fallimenti ADC, clipping, limiti dei guadagni, conservazione della
riga di stato, decodifica EGNOS senza tempo e blocco MT0 attraverso la prima
disponibilita' del tempo. Build Windows normale con profiling OFF.

Il log hardware storico del 29 ottobre mostra una taratura fisica conclusa
a LNA 32 / VGA 56 con sigma I/Q circa 24.7, ma non e' una nuova prova della
dashboard Linux. Test automatici, replay RF e stato del servizio non
certificano l'aggancio dei GEO o la resa dell'autotune nell'ambiente live
dell'utente. Non si dichiara risolta la scarsa disponibilita' RF EGNOS.

Nel replay finale combinato, la prima sincronizzazione SBAS con CRC valido
compare fra 8 e 9 s della timeline e MT0 fra 11 e 12 s, prima del primo PVT a
55 s. Restano 24 soluzioni con misure, 12 previsioni e 10 perdite RF. Il replay
SiGe del binario finale conserva 23 soluzioni e zero perdite, senza applicargli
il riferimento geografico HackRF.

Log e riferimenti del replay di verifica sono nell'archivio
`development/profiling/adaptive-kalman-20261030`.
