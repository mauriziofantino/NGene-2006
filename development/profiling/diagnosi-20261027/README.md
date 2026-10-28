# Evidenze diagnostiche 27 ottobre 2026

Questi sono risultati derivati; RF e log integrali restano in `recordings/`
e `build/profile-*`. Riferimenti, hash dei binari, configurazioni, conteggi e
tempi effettivi sono in `runs.json`. `reference.json` conserva il riferimento
WGS84 fornito dall'utente, con quota ellissoidica.

- `live/`: 137 fix Linux, sessione precedente alla registrazione RF.
- `replay/`: replay misto 8 GPS / 8 Galileo / 1 EGNOS, 40 fix e 14 perdite.
- `gps-only/`: stesso RF, 8 GPS / 0 Galileo / 0 EGNOS, 63 fix e 8 perdite.
- `pvt-qr-mixed/`, `pvt-qr-gps/`: ripetizione dopo le correzioni PVT/QR;
  stessa disponibilita' fix e perdite, nessun miglioramento metrico dimostrato.
  `run.json` conserva gli hash dei binari e i riferimenti ai log.
- `iq-summary.json`, `iq-per-second.csv`: distribuzioni del CI8 completo.
- CSV di fase GPS: matched filter indipendente in double, salto comune
  intorno a 77.073 s gia' nel file RF.
- `diagnosi-ring-check.log`: confronto byte per byte dell'intero file, due
  modalita' di lettura, ring produzione.
- `diagnosi-fft-benchmark.log`: sei coppie alternate sullo stesso processo,
  trasformata accettata esattamente uguale.
- `profile-before.csv`, `profile-after.csv`: profiling temporaneo del replay;
  tempo monotono, con deschedulazioni, non puro tempo CPU per funzione.
- `accuracy-enu.png`, `accuracy-height.png`: confronti con ground truth.

I replay mantengono lo scheduling corrente, inclusa la quota Galileo. Le
epoche valide e i satelliti differiscono: la migliore RMS GPS-only non isola
un errore Galileo. La saturazione ADC e il salto di fase non identificano
da soli l'origine hardware/USB. I tempi completi sono molto variabili: il
microbenchmark FFT non dimostra uno speedup equivalente del ricevitore.

Istruzioni per ripetere le misure nel [README del laboratorio](../README.md);
interpretazione dettagliata nel [report](../../../OPTIMIZATION_REPORT.md),
sezioni 10-13. Profiling aggiuntivo rimosso dalla produzione dopo le prove.
