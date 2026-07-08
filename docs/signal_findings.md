# Toyota Yaris Hybrid XP130 (2014, THS-II NoDSU) — CAN bus signal findings

Log analizzato: `data/logs/session_0044.log` (candump, 500 kbps).

**Punto di aggancio fisico (correzione importante rispetto alla prima stesura di questo documento): il log NON è stato catturato dalla porta OBD-II.** È stato catturato collegando l'ESP32 al **connettore da 28 pin dell'autoradio di serie (part number 90980-12555)**, sui pin **9 (CAN-H, beige) e 10 (CAN-L, bianco)** — lo stesso punto dove normalmente si aggancia la scatoletta CAN-bus dell'autoradio Android cinese ("simple soft"), che per questa cattura era fisicamente scollegata e sostituita dall'ESP32. Vedi `pinoutguide.com` per il pinout completo di questo connettore.

Questo è rilevante per l'interpretazione dei segnali assenti (punto 2 e 8 sotto): dato che il set di messaggi visti su questo connettore (SPEED, RPM, GEAR, BRAKE, GAS_PEDAL, TURN_SIGNALS, ODOMETER, SEATS_DOORS, LIGHT_STALK, ecc.) è essenzialmente lo stesso che ci si aspetterebbe sul bus HS-CAN principale, è probabile che questo connettore sia un aggancio dello **stesso bus logico** usato anche dall'OBD-II (o comunque di un bus che porta lo stesso traffico broadcast), non un bus separato e "più ricco". Di conseguenza, il fatto che SOC/EV-mode/range non compaiano nemmeno qui **non si spiega più con "bus diverso non raggiungibile"**, ma punta più decisamente verso l'ipotesi che la scatoletta cinese ottenga quei dati con **richieste diagnostiche attive (UDS/ISO-TP)** inviate sul bus, non con puro ascolto passivo — esattamente come la documentazione Prius (EAA-PHEV, PriusChat) descrive per SOC/temperatura batteria HV, disponibili solo via richieste Mode 21 solecitate all'ECU batteria, non come broadcast. Verifica in corso: prossimo log con la scatoletta ricollegata e ESP32 in ascolto in parallelo (vedi conversazione).

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

## 2. Autonomia residua / SOC HV — ✅ SOC TROVATO (vedi Addendum 2) / range km non trovato

> **AGGIORNAMENTO (Addendum 2, in fondo al documento): il SOC della batteria HV È
> presente come broadcast passivo — ID 0x4A7, byte[2], formula `SOC% = raw * 0.5`,
> validato fisicamente su entrambi i log.** Era sfuggito a tutte le scansioni
> precedenti perché quelle cercavano correlazione *istantanea* con l'acceleratore,
> mentre un SOC è l'*integrale* della potenza: a lag zero la correlazione è quasi
> nulla. La sezione qui sotto resta come storia della ricerca (e resta valida per
> l'autonomia residua in km, tuttora assente dal bus).

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
4. **Aggiornamento importante**: questo log è stato catturato non da OBD-II ma direttamente sul connettore da 28 pin dell'autoradio (pin 9/10 CAN-H/CAN-L, part number 90980-12555) — lo stesso punto dove normalmente è collegata la scatoletta CAN cinese, che *riesce* a mostrare SOC/EV-mode sullo stesso veicolo. Dato che il set di messaggi osservato su questo connettore coincide con quello che ci si aspetta sul bus HS-CAN principale (SPEED, RPM, GEAR, freni, ecc. tutti presenti e validati), è verosimile che **non sia un bus diverso/più ricco**, ma lo stesso bus logico. Questo sposta il sospetto principale dall'ipotesi "bus separato non raggiungibile" verso l'ipotesi **"la scatoletta interroga attivamente l'ECU batteria con richieste diagnostiche (UDS/ISO-TP), mentre il nostro ESP32 finora ha solo ascoltato passivamente"** — esattamente il meccanismo descritto nella documentazione Prius (SOC/temperatura HV disponibili solo via richiesta Mode 21 solecitata, non come broadcast). Non è comunque escluso al 100% un bus fisicamente diverso raggiunto da altri pin non ancora ispezionati.

**Cosa servirebbe per continuare (piano in corso):**
- **Test principale**: ricollegare la scatoletta cinese e mettere l'ESP32 in ascolto passivo IN PARALLELO sullo stesso connettore, catturando un log mentre si apre la schermata "flusso energia" sull'autoradio (che impiega ~10s a caricare — sintomo tipico di un roundtrip diagnostico attivo). Se in quella finestra compaiono ID nuovi mai visti nei log precedenti (es. richieste/risposte in stile UDS, tipicamente coppie di ID vicine in range come 0x7Dx/0x7Ex o simili), quello è il canale usato dalla scatoletta e possiamo decodificarlo.
- Se anche così non compare nulla di nuovo: provare un secondo punto di aggancio fisico (altri connettori menzionati nel pinout, es. 24/12/8 pin per varianti pre/post 2014) o inviare noi stessi richieste diagnostiche di prova (`isotpsend`/`cansend` con Mode 21/22) verso indirizzi ECU candidati.

**Confidenza: nessuna — assenza accertata; causa più probabile ora identificata (polling attivo) ma da confermare col prossimo log.**

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

## 8. EV mode — ✅ BIT REALE TROVATO (vedi Addendum 2)

> **AGGIORNAMENTO (Addendum 2): trovato un bit reale di "trazione elettrica
> attiva" — ID 0x498, byte[5] bit7 — validato su entrambi i log con semantica
> più ricca del proxy (spento da fermo anche a ICE spento). Trovato anche il bit
> "motore termico in moto": ID 0x245, byte[3] bit4.** La sezione sotto resta come
> storia del proxy, che è stato lo strumento con cui il bit reale è stato scovato.

**Bit esplicito**: come da ipotesi di partenza, il messaggio Prius `0x0529` (che su Prius Gen2 porta esattamente `EV-Mode Active {E:6}`, `EV-Mode Cancelled {E:7}`, `EV-Mode Denied {F:5,6,7}`) **non compare mai** in questo log (0 occorrenze, grep diretto). Nessun altro ID nel log porta un bit etichettabile come "EV mode" in nessuno dei DBC controllati. **Nota aggiornata**: questo log proviene dal connettore autoradio (pin 9/10, stesso punto della scatoletta cinese che invece mostra l'EV-mode), non da un ipotetico bus separato — quindi l'assenza qui punta più verso "serve una richiesta diagnostica attiva" che verso "bus irraggiungibile" (vedi punto 2 sopra per il ragionamento completo).

**Proxy costruito**: `EV_MODE_PROXY = (RPM(0x1C4) < 50) AND (SPEED(0x0B4) > 1 kph)` — cioè "il veicolo si muove ma il motore termico è di fatto fermo", condizione che su un ibrido THS-II implica trazione elettrica pura.

**Validazione**: nei 365.5s in cui il veicolo risulta in movimento (>1 kph), il proxy risulta vero per **193.8s (53.0%)**, con **23 transizioni** avanti/indietro nell'arco del log. Percentuale e numero di transizioni sono entrambi plausibili per una guida urbana a bassa velocità con motore che si accende/spegne di continuo (tipico comportamento THS-II) — né 0% (motore sempre acceso) né 100% (mai acceso), né un singolo blocco continuo (che indicherebbe un artefatto).

**Limite dichiarato**: è un segnale derivato/inferito da due segnali confermati, non un bit decodificato dal bus. Non distingue "EV mode" selezionato esplicitamente dall'utente da un normale spegnimento ICE gestito automaticamente dalla logica HSD (su questa piattaforma NoDSU senza un vero pulsante "EV mode" dedicato in molti mercati, la distinzione potrebbe essere comunque poco significativa).

**Confidenza: bit reale assente (confermato); proxy valido e ben comportato, ma dichiaratamente un'inferenza.**

---

## Riepilogo confidenza

| # | Segnale | Stato | ID | Confidenza |
|---|---|---|---|---|
| 1 | Marcia P/R/N/D | ✅ trovato | 0x127 `GEAR` | Validato |
| 2a | **SOC batteria HV %** | ✅ **trovato (Addendum 2)** | **0x4A7 byte[2] × 0.5** | **Validato su 2 log, scala da convenzione Toyota** |
| 2b | Autonomia residua km | ❌ non trovato | — | Assenza motivata |
| 3 | Temperatura (refrigerante?) | ⚠️ trovato | 0x618 byte[3] | Ipotesi forte, scala incerta |
| 4 | Velocità | ✅ confermato | 0x0B4 `SPEED` | Validato (già noto) |
| 5 | Frecce | ✅ trovato | 0x614 `TURN_SIGNALS` | Validato |
| 6a | Acceleratore % | ✅ trovato | 0x245 `GAS_PEDAL` | Validato |
| 6b | Freno bit | ✅ trovato | 0x230 `BRAKE_PRESSED` | Validato |
| 6c | Freno % / posizione | ⚠️ trovato | 0x224 byte[4:6] | Ipotesi forte, scala incerta |
| 7 | Giri motore | ✅ confermato | 0x1C4 `RPM` | Validato (già noto) |
| 8a | **EV / trazione elettrica (bit)** | ✅ **trovato (Addendum 2)** | **0x498 byte[5] bit7** | **Validato su 2 log** |
| 8b | **ICE in moto (bit)** | ✅ **trovato (Addendum 2)** | **0x245 byte[3] bit4** | **Validato su 2 log** |
| 9 | Carburante | ⚠️ **riclassificato (Addendum 2)** | 0x3A0 byte[7] | Contatore di consumo, NON livello serbatoio |
| 10 | Domanda di potenza/accel. (segno) | ⚠️ trovato (Addendum 2) | 0x320 byte[4] s8 | Ipotesi forte sulla semantica, scala incerta |

File di output completo: `dbc/toyota_yaris_xp130_reversed.dbc`.

---

## Addendum — sessione 0047 (ESP32 inline tra scatoletta CAN e auto)

Secondo log (`data/logs/session_0047.log`, 272 511 frame, 440s, 100 ID unici) catturato collegando l'ESP32 **in mezzo** tra l'ingresso della scatoletta CAN dell'autoradio Android e i cavi auto (box riconnesso, non più scollegato come in session_0044).

**Bonus — GEAR ora validato su tutti e 5 gli stati**: dai marcatori a freno lasciati dall'utente si ricostruisce l'intera sessione — fermo in P per ~98s (nessuna marcia innestata, coerente con "a fermo non compare nulla" sul display), poi P→N→R→N→D a t≈99-108s, marcia D ininterrotta per ~280s, poi un cluster di 5-6 frenate a t≈389-391s seguito da un giro completo delle marce **R→D→B→D→N→R→D→P**. È il primo avvistamento dello stato **B (Engine Brake, valore 4)**, che completa la validazione di `VAL_ 295 GEAR` su tutti i 5 stati (P/R/N/D/B) invece dei soli 3 (P/R/D) visti nella prima sessione.

**Ricerca del canale SOC/power-flow — risultato negativo ma informativo**:
- Diff degli ID unici tra le due sessioni: **10 ID compaiono solo in session_0047** (con la scatoletta ricollegata): `0x2FC 0x387 0x389 0x38E 0x38F 0x434 0x435 0x443 0x458 0x6F9`. Nessun ID è sparito.
- Controllo incrociato: nessun byte di un ID già noto passa da "costante" (in 0044) a "variabile" (in 0047) — la scatoletta non "risveglia" segnali dormienti già mappati.
- **Tutti e 10 gli ID nuovi hanno un unico valore esadecimale fisso per l'intera sessione di 440s**, comprese le frenate, i cambi marcia e ~5 minuti di guida reale (es. `0x387` sempre `000000003f3f3f3f`, `0x434` sempre `414000c000000000`, ecc. — vedi tabella completa nei log della conversazione). Alcuni contenuti (es. `0x434` che inizia con `0x41`, `0x40`) ricordano il formato classico di risposta OBD2 (Service ID + 0x40), suggerendo un handshake/scambio di capacità diagnostiche a sessione fissa, ripetuto come keep-alive — non un canale dati che trasporta un valore che cambia.
- **Conclusione**: questi 10 ID nuovi NON possono essere il canale del flusso energia/SOC che l'utente vede aggiornarsi in tempo reale sull'autoradio, perché il loro contenuto non varia mai. Su due catture indipendenti dello stesso bus (connettore autoradio, ~850s di guida reale in totale, 100 ID unici totali) non è mai comparso alcun segnale con una dinamica compatibile con SOC/EV-mode/power-flow.
- **Ipotesi rivista dopo conferma dell'utente**: l'utente ha verificato fisicamente che il cablaggio è quello Toyota di serie (CAN entra nella scatoletta, esce verso l'autoradio), senza OBD-II né Bluetooth aggiuntivi. Il box **RP5-TY-101/"XP Simplesoft"** usato qui è già stato reverse-engineerato da terzi: repo GitHub `zugetor/simplesoft-canbus-box-reverse-engineer`. Il box legge il CAN veicolo e lo ritraduce in "function ID" standardizzati per l'head unit; il **function ID 0x1F ("OilElectricityInfo")** porta esattamente: byte0 bit0-2 = livello batteria a 3 bit (0-7, "a barre" come lo storico 0x0529 di Prius), bit7 = flag ibrido; byte1 = 6 flag di percorso energia (batteria→motore, motore→ruote, motore termico→motore elettrico, motore termico→ruote, batteria→motore, frenata rigenerativa). Il documento non elenca però quale ID CAN *lato auto* alimenti questo function ID — è il box stesso a calcolarlo.
- **Importante**: entrambe le catture (0044 e 0047) sono state fatte sul lato **auto→scatoletta** (input della scatoletta), mai sul lato **scatoletta→autoradio** (output). Se il box traduce/sintetizza, l'unico segmento che garantisce di vedere il valore "already-computed" a 3 bit è quello mai ancora catturato.
- **Scansione sistematica estesa** (byte interi, nibble alto/basso, correlazione con l'acceleratore, ricerca di trend lenti tipo il candidato temperatura) su tutti i 100 ID, entrambe le sessioni: un candidato iniziale molto promettente (`0x4A8` byte2 top-3-bit, sembrava salire/scendere in modo plausibile con marce e frenate in session_0047) è stato **ritrattato dopo verifica incrociata su session_0044**, dove mostra chiaramente un comportamento a contatore ciclico (risale ripetutamente da 0 a 7 e si "avvolge" più volte in 350s) — fisicamente incompatibile con una batteria HV reale, che non si svuota e riempie ogni 60-100 secondi. Nessun altro candidato ha superato la verifica di plausibilità fisica (o sono contatori/eventi sporadici, o correlano troppo perfettamente con l'acceleratore per essere altro che un doppione del segnale gas già noto, es. `0x361` byte2 e `0x49b` byte0, correlazione ~1.0 con `GAS_PEDAL`).
- **Bonus laterale — candidato SERBATOIO/carburante trovato**: `0x3A0 byte[7]`, unico byte attivo di un messaggio altrimenti tutto a zero, trasmesso a 10 Hz. In **entrambe** le sessioni indipendenti decresce lentamente e quasi monotonicamente per tutta la guida (session_0044: 61→53 in 350s; session_0047: 58→52 in 430s), con piccole oscillazioni fra valori adiacenti (rumore tipico di un galleggiante che sciaborda) ma **senza mai risalire o avvolgersi**. Comportamento da manuale per un livello carburante che si consuma lentamente. Scala/unità non note (litri? frazione di un fondo scala sconosciuto?). Confidenza: ipotesi forte.
- **Conclusione per SOC/EV-mode**: non trovato nemmeno con questa scansione più estesa. Prossimo passo consigliato: catturare il lato **output della scatoletta verso l'autoradio** (non più l'input lato auto, già fatto due volte), per vedere direttamente il valore a 3 bit del function ID 0x1F che la scatoletta sintetizza, e da lì risalire per correlazione a cosa lo alimenta lato auto.

Script aggiunti per questa analisi: `tools/diff_byte_activity.py` (cardinalità valori fra due log), `tools/find_battery_bars.py` (scansione enum a bassa cardinalità), `tools/find_soc_like.py` (correlazione con l'acceleratore), `tools/find_nibble_levels.py` (stessa ricerca a livello di nibble).

---

## Addendum 2 — SOC HV, bit EV e bit ICE TROVATI (analisi derivativa sui log esistenti)

Questa sessione di analisi non ha usato nuovi log: ha ri-analizzato session_0044 e
session_0047 con due metodi nuovi, entrambi motivati da un errore metodologico
identificato nelle scansioni precedenti.

### Perché le scansioni precedenti non l'avevano trovato

Tutte le ricerche SOC precedenti (`find_soc_like.py`, `find_battery_bars.py`,
`find_nibble_levels.py`) cercavano **correlazione istantanea** con l'acceleratore o
enum a bassa cardinalità. Ma un SOC è l'**integrale** della potenza scambiata: a lag
zero la correlazione col pedale è quasi nulla, e su un log di 7 minuti un SOC
percorre 20-70 valori raw distinti (fuori dal range "barre 0-8" cercato da
find_battery_bars). Il nuovo `tools/find_soc_derivative.py` valuta invece la
**fisica di ogni singola variazione**: un SOC vero deve scendere durante la guida
elettrica e salire con ICE acceso o in frenata rigenerativa. Analogamente,
`tools/find_ev_bit.py` scansiona ogni bit di ogni ID misurando l'accordo con il
proxy EV già validato (RPM<50 AND speed>1), su entrambi i log.

### ✅ SOC batteria HV: ID 0x4A7, byte[2], `SOC% = raw × 0.5` — VALIDATO

- **Messaggio**: 0x4A7, 8 byte, ~2 Hz (famiglia 0x498-0x4AF, vedi sotto).
- **Campo**: byte[2], intero a 8 bit. Range osservato: 108–143 (0044) e 108–135
  (0047) → **54.0–71.5%** con scala 0.5 — esattamente la banda operativa di un
  THS-II reale (il sistema regola il SOC NiMH tra ~40% e ~80%).
- **Validazione fisica su entrambi i log** (`tools/plot_soc_timeline.py 4a7 2`):
  - fermo in P con tutto spento: piatto (108 = 54.0% per i primi 32s/94s);
  - all'accensione dell'ICE da fermo (warmup, ~1300rpm): salita ripida e
    continua 108→128 (carica forzata da fermo, comportamento THS-II da manuale);
  - ogni tratto EV (bit EV=1, RPM=0): discesa lenta e regolare ~0.5% ogni 4-6s;
  - ogni tratto con ICE in carica: risalita; **mai** un wrap o un salto;
  - punteggio fisico del rilevatore derivativo: 0.88 (0044) e 0.98 (0047), il
    più alto di tutti i 100 ID in entrambi i log.
- **Scala**: 0.5%/bit è la convenzione Toyota nota (stessa risoluzione del SOC
  Techstream e della formula Prius Gen2 `(256C+D)/2`). Non abbiamo ancora una
  lettura di riferimento Techstream/Dr.Prius per la conferma assoluta, ma banda
  (54-71%) e dinamica sono fisicamente perfette con questa scala.
- Nota onesta: entrambi i log partono esattamente da raw=108 (54.0%). Con due soli
  log non è distinguibile una coincidenza da un eventuale "valore di default
  post-accensione" durante l'inizializzazione della ECU batteria. I primi secondi
  del prossimo log lungo chiariranno.
- Perché 0x3CB/0x03B (gli ID SOC noti di Prius Gen2/Gen3) non compaiono: questa
  piattaforma (XP130/NHP10, 2011+) usa evidentemente uno schema diverso — la
  famiglia 0x49x/0x4Ax (vedi sotto).

### ✅ Bit "trazione elettrica attiva" (EV): ID 0x498, byte[5] bit7 — VALIDATO

Scansione esaustiva bit-per-bit contro il proxy EV, poi disambiguazione sui 4
stati fisici (fermo/movimento × ICE acceso/spento) con `tools/ev_bit_states.py`:

| stato | 0044 | 0047 |
|---|---|---|
| fermo, ICE off | 3.0% | 11.1% |
| fermo, ICE on | 0.0% | 0.0% |
| **movimento, ICE off** | **100.0%** | **100.0%** |
| movimento, ICE on | 8.9% | 3.5% |

Acceso **solo** quando il veicolo si muove in elettrico puro; spento da fermo
anche con ICE spento (i residui 3-11% da fermo sono l'isteresi negli istanti a
cavallo dell'arresto, e gli 3-9% in movimento/ICE-on sono il ritardo di
transizione mentre l'ICE riparte). È la semantica esatta della spia "EV" del
cruscotto — più ricca del proxy (che non distingue il fermo). 23/23 e 5/5
transizioni in accordo col proxy nei due log.

Bit affini trovati e validati nella stessa scansione (utili come ridondanza):
- **0x49C byte[5] bit6** e **0x4AD byte[3] bit6**: "ICE spento" puro (accesi al
  100% con ICE fermo sia in moto che da fermo, ~0-8% con ICE acceso).

### ✅ Bit "motore termico in moto" (ICE): ID 0x245, byte[3] bit4 — VALIDATO

Nel messaggio GAS_PEDAL già noto: acceso al 98-99% quando RPM>~800 e allo
0-0.7% quando l'ICE è fermo, **indipendentemente dal movimento**, su entrambi i
log. Più pulito e più pronto del thresholding sugli RPM; insieme al bit EV
copre l'intera macchina a stati del powertrain.

### La famiglia 0x498-0x4AF: frame "data recorder" del powertrain via gateway

Conferma esterna trovata: nel DBC di riferimento `toyota_2017_ref_pt.dbc` di
comma.ai (opendbc upstream) gli ID 1176-1199 (0x498-0x4AF) esistono come
messaggi **`ENG1D50`…`ENG1D60` emessi dal nodo `CGW` (Central GateWay)**, con
otto segnali opachi per byte chiamati `DRENGxx` — cioè frame di telemetria
del powertrain ("DR" = data recorder) inoltrati dal gateway, senza semantica
pubblica. Coerente con quanto osservato: è lì dentro che vivono SOC e stato
ibrido. Ulteriore conferma indiretta: sull'analisi TUCRRC di una Camry 2010
**non ibrida** (engr.colostate.edu/~jdaily/tucrrc/ToyotaCAN.html) questa
famiglia di ID non compare affatto — è specifica del powertrain ibrido — mentre
la stessa pagina conferma indipendentemente il nostro 0x224 byte[5:6] come
"Brake Pressure" (stessi byte!) e 0x611 come odometro.

### ⚠️ RICLASSIFICATO: 0x3A0 byte[7] NON è il livello del serbatoio

La verifica di plausibilità promessa è stata fatta, e l'ipotesi "livello
carburante" **non regge**:

- I decrementi correlano con il **tempo di ICE acceso**, non col tempo totale né
  con la distanza: 1 unità ogni ~20-25s di ICE in moto, in modo notevolmente
  coerente **tra i due log** (8 unità/193s-ICE in 0044, 6 unità/155s-ICE in 0047,
  ~±7% di differenza).
- Se fosse il livello di un serbatoio da 36L su una scala a byte (~0.4-0.6 L/unità),
  il tasso implicherebbe 50+ L/h di consumo: fisicamente impossibile (15-20×
  troppo veloce) per un 1NZ-FXE in guida urbana.
- In 0047 oscilla 58↔59 anche da fermo a ICE spento (rumore di lettura, non
  consumo).

**Nuova interpretazione (ipotesi forte)**: è un **contatore di carburante
consumato ad alta risoluzione che decresce** — quantum stimato ~10-30 mL
(1.4-3.7 L/h impliciti, giusti per guida urbana ibrida) — molto probabilmente il
**byte basso di un registro più ampio** ("carburante rimanente" ad alta
risoluzione) i cui bit alti non sono in questo messaggio. Predizione falsificabile:
su un log ≥30 min di guida si deve osservare il wrap 255→…→0→255. Uso pratico:
derivandolo si ottiene il **consumo istantaneo**; NON usarlo come livello assoluto.
Il livello "a barre" del quadro probabilmente non transita affatto su questo bus
(il galleggiante è cablato direttamente al quadro strumenti su questa piattaforma).

### ⚠️ NUOVO: 0x320 byte[4] — domanda di potenza/accelerazione longitudinale (con segno)

Cercando una "corrente batteria HV" (campo con segno che si inverte tra scarica e
carica) con `tools/find_batt_current.py`, l'unico sopravvissuto ad alta frequenza
su entrambi i log è **0x320 byte[4]** (s8, ~20 Hz): positivo accelerando
(+10..+31 col pedale premuto), negativo frenando o in rilascio (-8..-48), ~0 da
fermo E in crociera costante. La coppia (segno ↔ trazione/frenata) però resta
positiva anche in trazione EV e non diventa negativa nella carica da fermo →
**non è la corrente batteria**: è una **domanda di coppia/accelerazione
longitudinale**. byte[7] è lo stesso segnale con offset costante +42, byte[5] è
un flag {0,8} attivo solo a veicolo fermo. Scala non confermata (plausibile
~0.02-0.05 m/s² per LSB). Una vera corrente/potenza batteria con segno **non è
presente in broadcast** su questo bus (confermato: il Prius 0x03B non esiste qui).

### Il mistero della scatoletta è risolto (senza catturare l'output)

Con questi risultati, il function ID 0x1F "OilElectricityInfo" della Simplesoft
è interamente sintetizzabile in **puro ascolto passivo** dei segnali ora mappati:

- *livello batteria a 3 bit (0-7 barre)* ← quantizzazione di **SOC 0x4A7 byte[2]**;
- *flag "hybrid"* ← presenza della famiglia 0x49x;
- *frecce del flusso energia* ← combinazione di **bit EV (0x498 b5.7)**, **bit ICE
  (0x245 b3.4)**, **segno di 0x320 byte[4]** (trazione vs regen) e velocità.

Non serve più ipotizzare polling diagnostico attivo: i ~10s di ritardo della
schermata sono verosimilmente solo inizializzazione dell'app dell'autoradio. I 10
ID costanti apparsi in session_0047 restano un handshake/keep-alive della
scatoletta, come già concluso.

### Dettagli utili per la cattura (facoltativa) del lato scatoletta→autoradio

Dal `decoder.py` del repo zugetor (verificato direttamente nel sorgente):
**UART TTL a 38400 baud, 8N1**. Frame: `[0x2E, FunctionID, DataLength,
Data..., Checksum]` con `Checksum = (somma di FunctionID+Length+Data) & 0xFF ^ 0xFF`.
Con un ESP32: collegare RX a 38400 sul filo TX della scatoletta verso
l'autoradio (solo ascolto, GND comune). Ora serve però solo come *verifica di
conferma* (vedere il livello a 3 bit e confrontarlo con 0x4A7), non più come
unica strada per il SOC.

### Cosa resta aperto

1. **Conferma di scala SOC** contro una lettura di riferimento (Techstream,
   Dr. Prius via OBD dongle, o la cattura UART di cui sopra). La dinamica è
   inattaccabile; solo il fattore 0.5 è "da convenzione".
2. **Autonomia residua in km**: tuttora nessun candidato su questo bus (l'unico
   contachilometri è l'odometro 0x611). Probabilmente calcolata dal quadro
   strumenti internamente e mai pubblicata.
3. **Wrap del contatore carburante 0x3A0**: serve un log ≥30 min per la conferma
   definitiva dell'ipotesi "byte basso di contatore".
4. **0x612 byte[5]**: random-walk lento centrato su 128 (±16), byte[1] è un
   bit di heartbeat che alterna 0x80/0x00. Escluso come SOC (nessuna coerenza
   con gli stati di carica/scarica). Non identificato — possibile corrente
   batteria 12V con offset 128 o un trim. Bassa priorità.
5. **0x4AE byte[7]** (72-75, sale lentamente in entrambi i log): debole candidato
   temperatura batteria HV/inverter. Da riguardare su un log lungo estivo.

Script aggiunti in questa sessione: `find_ev_bit.py` (scansione bit vs proxy EV),
`ev_bit_states.py` (tabella 4-stati per disambiguare EV vs ICE-status),
`find_soc_derivative.py` (rilevatore SOC a fisica derivativa),
`plot_soc_timeline.py` (timeline ASCII con contesto veicolo),
`find_batt_current.py` (ricerca campi con inversione di segno carica/scarica).
