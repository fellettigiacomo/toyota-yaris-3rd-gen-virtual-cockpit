# Toyota Yaris Hybrid XP130 (2014, THS-II NoDSU) — CAN bus signal findings

Log analizzato: `data/logs/session_0044.log` (candump, HS-CAN via OBD-II, 500 kbps).

- 337 005 frame totali, 90 ID unici.
- Le prime 193 righe portano un timestamp RTC non impostato (epoch enorme e non monotono, da cui il nome del file `RTCUNSET`); sono state scartate. Il resto è un'unica sessione continua e monotona di **430.4 secondi (~7.2 minuti)** di guida reale.
- Durata breve: sufficiente per validare fisicamente P→R→D, frecce, freno, acceleratore, ma **troppo corta per catturare un ciclo di scarica/ricarica HV completo o un pieno di carburante consumato** — questo limita cosa si può confermare per punti 2 e 3.

Metodologia: script Python in `tools/` (`parse_log.py`, `id_stats.py`, `byte_stats.py`, `decode_dbc.py`, `find_temp_like.py`) + libreria `cantools` per decodificare rigorosamente il bit-layout Motorola dei DBC, invece di calcolarlo a mano. DBC di riferimento: `toyota_nodsu_hybrid_pt_generated.dbc` da `BogGyver/opendbc` (branch `tesla_unity_dev`), incrociato anche con `toyota_prius_2017/camry_hybrid/highlander_hybrid_*_generated.dbc` e con i DBC "grezzi" di `commaai/opendbc` upstream. Per temperatura/SOC/range, nessun DBC opendbc contiene questi segnali (openpilot non ne ha bisogno); ho quindi cercato documentazione di reverse engineering della community Prius (EAA-PHEV wiki, PriusChat Gen2 CAN/OBD2 PID list di Atilla Vass/usbseawolf2000, opengarages.org).

Tabella riassuntiva finale: vedi `dbc/toyota_yaris_xp130_reversed.dbc`.

---

## 1. Marcia (P/R/N/D) — ✅ CONFERMATO

- **ID 0x127, message `GEAR_PACKET`, segnale `GEAR`**: 4 bit, byte 5 nibble alto (bit 47, Motorola).
- `VAL_ 295 GEAR`: 0=P, 1=R, 2=N, 3=D, 4=B.
- **Validazione fisica** (vedi `tools/decode_dbc.py` + query mirata sulle transizioni):

  | t (s) | GEAR | SPEED (0xB4) |
  |---|---|---|
  | 0.01 | P (0) | 0 kph, fermo |
  | 50.44 | R (1) | resta 0 fino a 51.8s, poi sale 0→4.7 kph, ridiscende a 0 per 58s (manovra in retromarcia) |
  | 60.15 | D (3) | resta 0 fino a 61.5s, poi sale 0→10+ kph (partenza in marcia avanti) |

  Sequenza perfettamente coerente con "uscita in retromarcia da un parcheggio, poi marcia avanti". BRAKE_PRESSED (0x230) rilasciato esattamente a 50.73s (un attimo prima dello shift in R) e ripremuto durante le soste — ulteriore conferma incrociata.

- **Nota su una vecchia ipotesi ritrattata**: 0x127 era stato sospettato come "contatore lineare" e non come marcia. In realtà quel contatore è **`CAR_MOVEMENT`**, un campo *diverso* nello stesso messaggio (byte 4, valori oscillanti con segno -34..+73, verosimilmente un delta di movimento/impulsi ruota) — `GEAR` è un campo separato (nibble alto del byte 5) e si comporta esattamente come una marcia: **solo 3 valori distinti in tutto il log (0, 1, 3)**, che cambiano solo 2 volte in 430s.

**Confidenza: validato empiricamente con narrativa fisica completa.**

---

## 2. Autonomia residua / SOC HV — ❌ NON TROVATO (con giustificazione)

Cercato sia "range residuo km" sia, come da mandato, "% carica batteria HV" come proxy.

**Cosa è stato trovato:**
- **ID 0x611 `UI_SETTING`, segnale `ODOMETER`**: CONFERMATO ma è il **contachilometri totale** (99552 → 99555 km in ~3 km percorsi durante il log), non l'autonomia residua. `UNITS`=1 (km).
- Nessun segnale che assomigli a "distance to empty" o "% SOC" è stato trovato in nessuno dei 90 ID del log.

**Perché manca, con evidenza:**
1. Nessun DBC di `opendbc` (comma.ai, BogGyver, e derivati controllati: nodsu_hybrid/nodsu/new_mc/tnga_k/prius_2017/camry_hybrid/highlander_hybrid) definisce SOC, range o percentuale batteria — openpilot non ne ha bisogno per il controllo laterale/longitudinale, quindi questi segnali non sono mai stati reverse-engineerati in quel progetto.
2. La documentazione della community Prius (EAA-PHEV wiki, PriusChat "Prius Gen2 (2009) CAN/OBD2 PID Codes" di Atilla Vass) mostra che sulla piattaforma Prius Gen2 il SOC è disponibile in due modi:
   - **Un broadcast non richiesto su ID 0x03CB** (formula `(256*C+D)/2` = %, range 40-80%), a ~108ms.
   - **Il messaggio 0x0529** (una volta al secondo), che porta anche le barre SOC del display *e i bit EV-Mode Active/Cancelled/Denied*.
   - Il dettaglio completo (voltaggio per singolo blocco cella, temperatura, corrente) è disponibile **solo via richiesta diagnostica UDS solecitata** (Mode 21, PID 0xC3/0xCE, ECU 0x7E2/0x7E3), NON come broadcast periodico.
3. **Ho verificato con grep diretto sul log che né 0x3CA, 0x3CB, 0x3CC, 0x3CD, né 0x529 compaiono mai** — zero occorrenze, in linea con quanto già osservato dall'utente per 0x529 durante centinaia di migliaia di frame di guida reale.
4. Questo è coerente con l'architettura NoDSU/Yaris (generazione e piattaforma diversa dal Prius Gen2): gli ID del nostro log non coincidono affatto con lo schema Prius Gen2 nemmeno per segnali comuni già confermati (es. Marcia è 0x127 qui contro 0x120 sul Prius Gen2; Velocità 0x0B4 qui, che sul Prius Gen2 risultava "sconosciuto"/non decodificato dalla community). È quindi plausibile che il bus HV/batteria su questo veicolo viva su un segmento CAN separato (bus "ibrido"/inverter), dietro un gateway non raggiungibile dalla porta OBD-II — esattamente come già ipotizzato per 0x529.

**Cosa servirebbe per continuare:**
- Un accesso diagnostico attivo (invio richieste UDS Mode 21/22 con `cansend`/`isotp` verso possibili ECU indirizzi tipo 0x7E2/0x7E3 o equivalenti su questo modello) invece di sola cattura passiva.
- Oppure un punto di aggancio fisico diverso (es. connettore diretto sul bus ibrido/inverter, se esiste e fisicamente accessibile, non solo OBD-II).
- In alternativa, un log più lungo con più cicli di carica/scarica potrebbe comunque non bastare se il segnale semplicemente non è instradato sull'HS-CAN visibile da OBD-II.

**Confidenza: nessuna — assenza accertata e motivata, non solo "non cercato".**

---

## 3. Temperatura — ⚠️ IPOTESI FORTE (non confermata su scala esatta)

**Candidato: ID 0x618, byte[3]** (famiglia di messaggi "combination meter" multiplexati a bassa frequenza: 0x610-0x615, 0x618-0x61c, ciascuno con un ~0.1 Hz di frequenza e un byte[0] costante che sembra un indice di parametro interno, non collegato all'ordine numerico degli ID — es. byte[0] vale 32,33,34,35,36,38,39,40,41,42 per gli ID 0x610,0x611,0x618,0x619,0x61a,0x61c,0x612,0x613,0x614,0x615 rispettivamente).

**Serie temporale osservata** (uno ogni 10s):

| t (s) | valore grezzo | note |
|---|---|---|
| 4 – 64 | 0 | motore fermo, GEAR=P |
| 73 | 10 | poco dopo l'accensione/movimento (R a 50s, D a 60s) |
| 83–173 | 20→100 | salita quasi lineare |
| 183–343 | 120→230 | salita che rallenta (tipico avvicinamento a temperatura di regime) |
| 353–423 | 230→240 | plateau raggiunto, stabile per gli ultimi ~70s del log |

Questo è esattamente il profilo fisico di una **curva di riscaldamento del liquido di raffreddamento motore**: parte da un valore basso a motore spento/freddo, sale monotonicamente non appena il motore comincia a girare, e si stabilizza dopo circa 5 minuti quando il termostato regola la temperatura di esercizio — comportamento da manuale per un motore Toyota 1NZ-FXE (il propulsore Atkinson usato sulla Yaris Hybrid).

**Formula ipotizzata (scala NON confermata)**: `temp_C ≈ raw * 0.375`, che porrebbe il plateau a 90°C (plausibile). Il fattore 0.375 è una stima "a occhio" per far cadere il plateau in un range di normale funzionamento (80–95°C), **non è derivato da una lettura di riferimento con termometro**. Non è escludibile che sia invece temperatura del cambio/CVT o altro fluido che si scalda con dinamica simile — non distinguibile con i soli dati disponibili.

**Cosa servirebbe per confermare al 100%:** una lettura di riferimento (termometro a infrarossi sul radiatore/vaso di espansione, o uno scan tool che legga il PID standard Mode01 PID05 via richiesta diagnostica attiva) da confrontare punto a punto con questo valore.

**Confidenza: ipotesi forte, dinamica fisica fortemente indicativa, scala/formula non confermata.**

Nessun altro segnale di temperatura (es. temperatura batteria HV) è stato identificato — coerente col fatto che il bus batteria HV non è raggiungibile (vedi punto 2).

---

## 4. Velocità — ✅ GIÀ CONFERMATO, nessuna alternativa più precisa trovata

- **ID 0x0B4 `SPEED`**, formula già nota: `speed_kph = ((byte[5]<<8)|byte[4]) * 0.01`. Riconfermato via `cantools` (`SPEED` bit47|16@0+, scala 0.01).
- Verificato se esiste un ID più preciso: `WHEEL_SPEEDS` (0x0AA, per-ruota, potenzialmente più fine/utile per rilevare slittamento) definito nel DBC opendbc **non è presente nel log** (0 occorrenze via grep diretto). Nessun altro candidato di velocità trovato.
- `ENCODER` (byte[4] dello stesso 0x0B4) è probabilmente un contatore di impulsi ruota/risoluzione ausiliaria, non un ID separato.

**Confidenza: confermato, 0x0B4 resta l'unica fonte di velocità disponibile in questo log.**

---

## 5. Frecce / indicatori di direzione — ✅ CONFERMATO

- **ID 0x614 `STEERING_LEVERS`, segnale `TURN_SIGNALS`** (2 bit): `VAL_`: 3=none, 2=right, 1=left.
- Osservati tutti e 3 gli stati durante il log, con transizioni a tempi plausibili per una guida cittadina con più svolte (es. sequenze none→right→none, none→left→none con durate di alcuni secondi/decine di secondi, coerenti con l'uso reale della leva prima di una svolta/cambio corsia).
- `HAZARD_LIGHT` (bit separato nello stesso messaggio) presente ma sempre 0 in questo log (mai attivate le quattro frecce).

**Confidenza: confermato.**

---

## 6. Pressione acceleratore e pressione freno — ✅ CONFERMATO (acceleratore) / ⚠️ IPOTESI FORTE (freno)

### Acceleratore — CONFERMATO
- **ID 0x245 `GAS_PEDAL`, segnale `GAS_PEDAL`**: byte[2], formula `valore = byte[2] * 0.005` (range 0-1 = 0-100%).
- Range osservato: 0.0–0.35 (0–35%), con pattern di pressione/rilascio ripetuti perfettamente coerenti con una guida a bassa velocità stop-and-go, ciascuna pressione seguita a breve distanza da un aumento di RPM/velocità.

### Freno (bit) — CONFERMATO
- **ID 0x230 `BRAKE_MODULE2`, segnale `BRAKE_PRESSED`** (1 bit): transizioni correlate esattamente con i cambi marcia e le variazioni di velocità (vedi punto 1).

### Freno (percentuale/posizione) — IPOTESI FORTE, non nel DBC opendbc
Il DBC opendbc definisce `BRAKE_MODULE` (pressione **e** posizione pedale, 9 bit ciascuno) sull'ID **0x226** — **questo ID non è mai presente nel nostro log** (verificato con grep, 0 occorrenze). Non è quindi disponibile "di serie" secondo lo schema noto.

Tuttavia, l'**ID 0x224** (presente, frequenza ~41 Hz, la stessa di SPEED e BRAKE_MODULE2 — probabilmente la stessa ECU) mostra un campo a 16 bit (byte[4]<<8 | byte[5]) che **correla fortissimamente con BRAKE_PRESSED**:

| BRAKE_PRESSED | n campioni | media | mediana | max |
|---|---|---|---|---|
| 0 (rilasciato) | 12 027 | 0.04 | 0 | — |
| 1 (premuto) | 5 731 | 153.9 | 62 | 537 |

99.99% dei campioni con freno rilasciato hanno valore ≤5; l'84% dei campioni con freno premuto hanno valore >5. Analizzando un singolo evento di rilascio (t≈50s) il valore scende in modo smooth e continuo da ~500 a 0 in circa 700ms, esattamente come ci si aspetterebbe da un pedale rilasciato gradualmente — non un bit, un vero segnale analogico.

**Non è confermata l'unità di misura esatta** (pressione idraulica vs. posizione pedale, scala/max range esatto — il max osservato di 537 supera lievemente il range 0-511 a 9 bit ipotizzato dal template opendbc per BRAKE_MODULE, suggerendo forse 10 bit o un offset diverso).

**Cosa servirebbe:** una pressione del pedale a fondo corsa nota/misurata, o un confronto diretto con uno scan tool che legga "Master Cylinder Pressure"/"Brake Pedal Position" per calibrare la scala esatta.

**Confidenza: acceleratore confermato con formula esatta; freno-bit confermato; freno-percentuale ipotesi forte (ID/campo giusti, scala incerta).**

---

## 7. Giri motore — ✅ GIÀ CONFERMATO

- **ID 0x1C4 `ENGINE_RPM`**, formula: `rpm = ((byte[0]<<8)|byte[1], con segno) * 0.78125`. Riconfermato via `cantools`.
- Ricorrelato positivamente con l'accelerazione (0x245) e con le fasi ICE-on/ICE-off usate anche come base per il proxy EV mode (punto 8).

**Confidenza: confermato.**

---

## 8. EV mode — ❌ Bit esplicito NON TROVATO / ✅ PROXY costruito e validato

**Bit esplicito**: come da ipotesi di partenza, il messaggio Prius `0x0529` (che su Prius Gen2 porta esattamente `EV-Mode Active {E:6}`, `EV-Mode Cancelled {E:7}`, `EV-Mode Denied {F:5,6,7}`) **non compare mai** in questo log (0 occorrenze, grep diretto) — confermando e rafforzando l'ipotesi già retrattata dall'utente. Nessun altro ID nel log porta un bit etichettabile come "EV mode" in nessuno dei DBC controllati.

**Proxy costruito**: `EV_MODE_PROXY = (RPM(0x1C4) < 50) AND (SPEED(0x0B4) > 1 kph)` — cioè "il veicolo si muove ma il motore termico è di fatto fermo", condizione che su un ibrido THS-II implica trazione elettrica pura.

**Validazione**: nei 365.5s in cui il veicolo risulta in movimento (>1 kph), il proxy risulta vero per **193.8s (53.0%)**, con **23 transizioni** avanti/indietro nell'arco del log. Percentuale e numero di transizioni sono entrambi plausibili per una guida urbana a bassa velocità con motore che si accende/spegne di continuo (tipico comportamento THS-II) — né 0% (motore sempre acceso) né 100% (mai acceso), né un singolo blocco continuo (che indicherebbe un artefatto).

**Limite dichiarato**: è un segnale derivato/inferito da due segnali confermati, non un bit decodificato dal bus. Non distingue "EV mode" selezionato esplicitamente dall'utente da un normale spegnimento ICE gestito automaticamente dalla logica HSD (su questa piattaforma NoDSU senza un vero pulsante "EV mode" dedicato in molti mercati, la distinzione potrebbe essere comunque poco significativa).

**Confidenza: bit reale assente (confermato); proxy valido e ben comportato, ma dichiaratamente un'inferenza.**

---

## Riepilogo confidenza

| # | Segnale | Stato | ID | Confidenza |
|---|---|---|---|---|
| 1 | Marcia P/R/N/D | ✅ trovato | 0x127 `GEAR` | Validato |
| 2 | Autonomia / SOC HV | ❌ non trovato | — | Assenza motivata |
| 3 | Temperatura (refrigerante?) | ⚠️ trovato | 0x618 byte[3] | Ipotesi forte, scala incerta |
| 4 | Velocità | ✅ confermato | 0x0B4 `SPEED` | Validato (già noto) |
| 5 | Frecce | ✅ trovato | 0x614 `TURN_SIGNALS` | Validato |
| 6a | Acceleratore % | ✅ trovato | 0x245 `GAS_PEDAL` | Validato |
| 6b | Freno bit | ✅ trovato | 0x230 `BRAKE_PRESSED` | Validato |
| 6c | Freno % / posizione | ⚠️ trovato | 0x224 byte[4:6] | Ipotesi forte, scala incerta |
| 7 | Giri motore | ✅ confermato | 0x1C4 `RPM` | Validato (già noto) |
| 8 | EV mode | ❌/✅ | proxy derivato | Bit assente (motivato); proxy validato |

File di output completo: `dbc/toyota_yaris_xp130_reversed.dbc`.
