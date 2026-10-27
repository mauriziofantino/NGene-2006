# Contesto del progetto

Prima di modificare la catena GNSS, leggere `LOGBOOK.md` e
`OPTIMIZATION_REPORT.md`: contengono decisioni, misure e limiti delle verifiche.

Il main operativo deve restare privo di strumentazione sperimentale aggiuntiva.
Gli strumenti riutilizzabili sono in `development/profiling/`, esclusi dalla
build. Ripristinare la build senza profiling dopo ogni misura.

Conservare la precisione di mixer, correlatori, tracking e PVT; una maggiore
disponibilita' di fix non dimostra maggiore accuratezza. Valutare lo scheduling
e la distribuzione del carico prima di introdurre approssimazioni numeriche.

L'utente richiede di conservare la logica di acquisizione del singolo satellite
della versione SiGe IF: ricerca coarse, raffinamento Doppler, raffinamento
codice e conferma M-su-N. Distinguere gli adattamenti numerici I/Q e di dinamica
dalle modifiche alle decisioni, incluse le stime delle soglie. Non modificare
questa logica durante le ottimizzazioni senza una nuova indicazione esplicita.
Questo vincolo non richiede di annullare lo scheduling fra costellazioni.
