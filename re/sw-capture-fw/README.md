# sw-capture — caratterizzazione ADC dei tasti al volante (ESP32-C3 mini)

Terzo firmware del repo, complementare a `obd-capture` (CAN) e
`virtual-cockpit` (cluster). Esiste perché il tasto **MODE non transita sul
bus CAN** (vedi `docs/signal_findings.md`, Addendum 5, ritrattazione): i
comandi audio al volante sono un **partitore resistivo analogico** — ogni
tasto chiude una resistenza diversa tra la coppia di pin steering-switch
(SW) del connettore 28-pin dell'autoradio (90980-12555), e chi li legge (di
serie: la scatoletta CAN Simplesoft) misura un livello di tensione.

Questo firmware campiona quel livello con l'ADC di un **ESP32-C3 mini**
(board separata: il cluster ESP32-S3 resta intonso mentre si sonda l'auto)
e stampa su USB:

- righe `EVT` (sempre attive): ogni cambio di livello stabile, con
  timestamp, livello precedente/nuovo e durata — premendo i tasti uno alla
  volta, la tabella del ladder esce direttamente dal log;
- righe `LVL`: heartbeat del livello stabile corrente ogni 5s;
- stream CSV opzionale (`STREAM ON` / `STREAM OFF`, stessi comandi di
  obd-capture): `t_ms,min_mV,avg_mV,max_mV` ogni 20ms, per grafici offline
  (`tools/sw_adc_logger.py` lo cattura su file).

I valori stampati sono **millivolt lato pin** (calibrati): la tensione
sulla linea SW è `mV × 2` con il partitore 2:1 qui sotto.

## Cablaggio

### Prima di tutto: verifica col multimetro (2 minuti)

Resistenza tra i due pin SW del connettore 28-pin (comandi al volante:
tipicamente una coppia dedicata SW/SW-GND — vedi pinout 90980-12555 su
pinoutguide.com), **a connettore staccato o quadro spento**: deve leggersi
un valore alto/aperto a riposo e un valore basso, distinto e stabile, per
ogni tasto tenuto premuto. Questo conferma quale coppia di pin è il ladder
prima di collegarci qualsiasi cosa.

### Modo A — sniffing in parallelo alla scatoletta (misura "in esercizio")

La scatoletta resta collegata e fornisce lei la polarizzazione della linea.
Ci si aggancia in parallelo ad **alta impedenza**, senza iniettare nulla:

```
linea SW (al pin del connettore) ──[R1 100kΩ]──┬──> GPIO3 (ADC)
                                               │
                                          [R2 100kΩ]
                                               │
SW-GND (stesso connettore) ────────────────────┴──> GND ESP32-C3
```

- Partitore 2:1: anche se la scatoletta polarizza a 5V, al pin arrivano
  max ~2.5V — dentro il range calibrato dell'ADC C3 a 11dB. Se polarizza
  a 3.3V si legge comunque benissimo (passi del ladder = centinaia di mV).
- Carico aggiunto: 200kΩ verso massa — trascurabile rispetto a ladder
  (~0.1-3kΩ) e pull-up della scatoletta; dopo il collegamento verificare
  comunque che i tasti funzionino ancora sull'autoradio.
- **GND comune obbligatorio**, preso dal pin SW-GND del connettore (è il
  ritorno del ladder), non dalla carrozzeria a caso.
- **Mai** collegare 3.3V/5V dell'ESP32 alla linea SW mentre la scatoletta
  è connessa.
- Alimentare il C3 da USB (il laptop che logga) o da un 5V→USB pulito.

In questo modo si misura anche la **tensione di polarizzazione reale**
della scatoletta (il livello idle), che serve per dimensionare la lettura
definitiva nel cluster.

### Modo B — banco / scatoletta scollegata

Senza scatoletta nessuno polarizza la linea: la bias la fornisce l'ESP32.

```
3V3 ESP32-C3 ──[Rpull 1kΩ]──┬── linea SW
                            └──[R1 100kΩ]──┬──> GPIO3 (ADC)
                                       [R2 100kΩ]
                                            │
SW-GND ─────────────────────────────────────┴──> GND ESP32-C3
```

Idle ≈ 3.3V sulla linea (≈1650mV al pin); ogni tasto premuto forma un
partitore `Rpull / R_tasto` e produce il proprio plateau. Con 1kΩ di
pull-up i valori tipici Toyota (~0.1-3.3kΩ) si distribuiscono su quasi
tutta la scala.

Nota pin: GPIO3 = ADC1_CH3, scelto evitando i pin di strapping del C3
(GPIO2/8/9) e ADC2 (GPIO5, inaffidabile su C3). Configurabile in
`include/app_config.h`.

## Uso

```bash
cd sw-capture
pio run -t upload          # board: ESP32-C3 mini/SuperMini, porta USB nativa
pio device monitor         # righe EVT/LVL subito visibili

# per un log CSV su file (da tools/):
python3 ../tools/sw_adc_logger.py /dev/ttyACM0 ../data/logs/sw_ladder_test.csv
```

Protocollo di test suggerito (identico nello spirito alla cattura CAN, ma
stavolta il segnale è per-pressione, niente merge):

1. 30s senza toccare nulla (livello idle + rumore);
2. ogni tasto del pod, uno alla volta: 3 pressioni brevi + 1 tenuta 2s,
   annunciando a voce l'ordine (o annotandolo);
3. ripetere l'intero giro una seconda volta (ripetibilità dei plateau).

Risultato atteso nel log, per ogni pressione:

```
EVT 41213 2497 -> 812 (prev held 5210 ms)   <- pressione MODE
EVT 41455 812 -> 2496 (prev held 242 ms)    <- rilascio (242ms di tenuta)
```

Tabella da compilare con i risultati (poi va in
`docs/signal_findings.md`):

| Tasto | mV pin (idle bias reale) | V linea | note |
|---|---|---|---|
| (riposo) | | | |
| MODE | | | |
| VOL+ / VOL- / SEEK / ... | | | |

## Dopo la caratterizzazione

Con la tabella dei plateau in mano, l'integrazione nel cluster è una
soglia ADC + debounce (~20 righe nel firmware `virtual-cockpit`, evento
per-pressione immediato — nessun broadcast a 2Hz, nessun limite di
pressioni ravvicinate). Da decidere allora se leggere il ladder
direttamente dall'ESP32-S3 del cluster (stesso schema di partitore) o
tenere il C3 come ponte. Il vincolo elettrico resta identico: alta
impedenza, mai polarizzare la linea se la scatoletta è collegata.
