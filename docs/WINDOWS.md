# Windows nativo: compilazione e configurazione

Questo package usa soltanto Windows x64 nativo: Visual Studio clang-cl, Windows
SDK e runtime Microsoft. Non richiede Cygwin, MSYS2, MinGW, Bash o WSL.

## Comandi

Aprire PowerShell nella cartella principale del progetto:

```powershell
.\build.cmd
```

Per pulire gli oggetti compilati e ricompilare:

```powershell
.\build.cmd clean
```

Per pulire soltanto, senza ricompilare:

```powershell
.\clean.cmd
```

`clean.cmd` elimina gli oggetti compilati e `ngene.exe`; conserva impostazioni,
registrazioni e cache delle dipendenze. Per ricompilare dopo la pulizia usare
`build.cmd`.

Entrambi i comandi individuano Visual Studio, preparano le dipendenze e lanciano
cinque test. Non serve definire `$cmake`, modificare PATH o aprire un terminale
speciale. La pulizia conserva configurazioni, registrazioni e cache delle
librerie. La prima build richiede Internet per scaricare le dipendenze.

Installare una volta i componenti Visual Studio: C++ x64, Windows SDK, C++ Clang
Tools e CMake Tools. Le dipendenze vengono compilate dai sorgenti con ABI Microsoft:
OpenSSL 3.5.9, pthreads4w, PDCursesMod 4.5.4, libusb 1.0.30 e libfec. Versioni,
URL e checksum sono fissati in `scripts/build-native-windows.ps1`.

## Dove sono i file

```text
NGene Classic/
  build.cmd
  run.cmd
  ngene.exe
  configuration/
    GNSS_RX.conf
    Front_End.conf
    Acquisition.conf
    Tracking.conf
    PVT.conf
    Logs.conf
    ...
  otherData/
  logs/
  sampleConfigurationFiles/   modelli iniziali
  build/                     cache e oggetti di compilazione
```

NGene cerca sempre `configuration` e `otherData` accanto a `ngene.exe`, anche
se viene avviato da un'altra cartella. Non legge `NGENE_HOME`, `HOME` o cartelle
nascoste `.ngene`. Le impostazioni gia' presenti non vengono sovrascritte dalla
build. Per cambiare le impostazioni modificare `configuration`, non i modelli.

I log vengono salvati in `logs` accanto all'eseguibile, salvo un `Dump Path`
valido impostato in `configuration/Logs.conf`.

## Avvio USB

```powershell
.\run.cmd
```

Il launcher usa l'interfaccia a schermo e lavora dalla cartella del progetto.
Per vedere il log testuale:

```powershell
.\run.cmd -t -v
```

`configuration/GNSS_RX.conf` seleziona il frontend e l'ingresso:

```ini
[Used FrontEnd]
1
[Input method]
0
[SiGe native IQ]
1
[SiGe IQ intermediate frequency (Hz)]
38400
```

`Input method = 0` usa la USB. Il SiGe deve essere collegato, con firmware caricato
ed accessibile al backend WinUSB. Un errore `16c0/072f` assente riguarda il
dispositivo, non la cartella dei file di configurazione.

## Registrazione

```powershell
.\run.cmd --input recordings\native-ab.raw
```

Oppure, per un altro file:

```powershell
.\run.cmd --input "D:\registrazioni\segnale.raw"
```

`--input` forza l'elaborazione di quel file soltanto per quell'avvio. Non modifica
`GNSS_RX.conf`: il successivo `run.cmd` torna all'ingresso configurato. I percorsi
relativi passati al launcher sono relativi alla cartella del progetto.

La registrazione locale `recordings/native-ab.raw` contiene circa 48,55 secondi di
byte originali SiGe. Il flag `SiGe native IQ = 1` seleziona la decodifica I/Q.
Per configurare permanentemente l'ingresso da file impostare `Input method = 1`
e `Input file` nel file `GNSS_RX.conf`.

## Note tecniche

clang-cl mantiene l'assembly SIMD originale ma genera codice con ABI Microsoft.
Il ricevitore conserva `/Od` per i clobber mancanti nelle macro storiche; le
librerie sono ottimizzate separatamente. La build nativa non implica da sola
che la stabilita' USB dal vivo sia stata verificata.

La cartella `build` contiene solo file generati dalla compilazione:

| Cartella | Scopo |
| --- | --- |
| `native-downloads` | Archivi delle dipendenze con checksum verificati |
| `native-sources` | Sorgenti delle dipendenze compilate per Windows |
| `native-tools` | Interprete Perl necessario alla build di OpenSSL |
| `native-deps` | Librerie native e header pronti per il collegamento |
| `windows-native` | Oggetti, librerie e test generati da CMake |

Queste cartelle non sono versionate in Git. I vecchi log, confronti e archivi
locali sono stati eliminati. La registrazione e' in `recordings`, separata dalla
build e anch'essa esclusa da Git. Non serve per ricevere dalla USB.
