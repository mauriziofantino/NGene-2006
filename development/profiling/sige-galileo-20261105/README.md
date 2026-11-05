# SiGe Galileo: riferimento IF, 5 novembre 2026

Sul record `recordings/native-ab.raw` il riferimento nativo di 38400 Hz
introduce 100 Hz di differenza rispetto al percorso reale funzionante:
`4130400 - 16367600/4 = 38500 Hz`. Il confronto DLL/PLL interpreta lo scarto
come falso lock Galileo e corregge la portante di 125 Hz. Il lock puo' apparire
stabile, ma le pagine I/NAV non vengono decodificate.

Con lo stesso eseguibile e lo stesso record, cambiando solo l'IF nelle copie
isolate 4 GPS / 4 Galileo / 0 EGNOS:

| IF nativa | Fix totali | Fix con Galileo | Correzioni PLL | Perdite RF |
| --- | ---: | ---: | ---: | ---: |
| 38400 Hz | 23 | 0 | 4 | 0 |
| 38500 Hz | 23 | 13 | 0 | 0 |

Fino a due Galileo contribuiscono al PVT corretto. La configurazione e'
diagnostica: la configurazione operativa locale resta 8 GPS / 4 Galileo /
1 EGNOS. Con quest'ultima il record di 48.55 s termina prima di completare
le effemeridi Galileo: zero fix misti anche dopo la correzione, ma I/NAV
viene decodificato. Non allentare i controlli per nascondere questo limite.

Una prova separata Galileo-only conferma pagine valide in real IF e in
I/Q nativo a 38500 Hz, contro log navigazione vuoti a 38400 Hz.
La build finale, senza override IF nella copia 4/4/0, riproduce 23 fix,
13 con Galileo, zero correzioni spurie e zero perdite.

Ripetizione dalla radice (eseguire una prova per ciascuna IF):

```powershell
& development/profiling/profile-replay.ps1 -InputFile recordings/native-ab.raw -Frontend sige -SigeNativeIq 1 -SigeIqIfHz 38400 -GpsChannels 4 -GalileoChannels 4 -EgnosChannels 0 -NavigationLog -Repetitions 1
& development/profiling/profile-replay.ps1 -InputFile recordings/native-ab.raw -Frontend sige -SigeNativeIq 1 -SigeIqIfHz 38500 -GpsChannels 4 -GalileoChannels 4 -EgnosChannels 0 -NavigationLog -Repetitions 1
```

`results.json` conserva hash, riferimenti ai log e conteggi. I log completi
restano nelle directory locali `build/profile-*`. Le misure di durata non
costituiscono un benchmark. Nessuna ground truth applicata a SiGe, nessuna
conclusione su miglioramento dell'accuratezza o stabilita' USB live.

Modifiche operative: default IF, template e configurazione locale a 38500 Hz.
I valori espliciti restano rispettati. Acquisizione per satellite, stima soglie,
tracking, parser e solver invariati; nessuna strumentazione aggiunta al main.
Build finale Windows: 14/14 CTest, `NGENE_RX_PROFILING=OFF`.
