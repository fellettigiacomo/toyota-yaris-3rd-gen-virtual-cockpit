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

## 8. EV mode — ❌ Bit esplicito NON TROVATO / ✅ PROXY costruito e validato

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
| 2 | Autonomia / SOC HV | ❌ non trovato | — | Assenza motivata |
| 3 | Temperatura (refrigerante?) | ⚠️ trovato | 0x618 byte[3] | Ipotesi forte, scala incerta |
| 4 | Velocità | ✅ confermato | 0x0B4 `SPEED` | Validato (già noto) |
| 5 | Frecce | ✅ trovato | 0x614 `TURN_SIGNALS` | Validato |
| 6a | Acceleratore % | ✅ trovato | 0x245 `GAS_PEDAL` | Validato |
| 6b | Freno bit | ✅ trovato | 0x230 `BRAKE_PRESSED` | Validato |
| 6c | Freno % / posizione | ⚠️ trovato | 0x224 byte[4:6] | Ipotesi forte, scala incerta |
| 7 | Giri motore | ✅ confermato | 0x1C4 `RPM` | Validato (già noto) |
| 8 | EV mode | ❌/✅ | proxy derivato | Bit assente (motivato); proxy validato |
| 9 | Carburante | ⚠️ trovato | 0x3A0 `FUEL_RAW` | Ipotesi forte, scala incerta (rafforzata nel 3° giro) |
| 10 | Temperatura #2 (olio/CVT/batteria?) | ⚠️ trovato | 0x3B9 `TEMP2_RAW` | Ipotesi forte, natura/scala incerta (NUOVO, 3° giro) |
| 11 | Freno #2 (probabile ABS/VSC) | ⚠️ trovato, NON SOC | 0x4A2 `CHASSIS_BRAKE2` | Tentativo, esplicitamente escluso come batteria (3° giro) |

File di output completo: `dbc/toyota_yaris_xp130_reversed.dbc`. Vedi anche l'addendum "terzo giro di analisi" più sotto per il dettaglio delle voci 9-11 e per la ricerca (senza esito) di SOC/EV-mode.

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

## Addendum — terzo giro di analisi (nessun nuovo log disponibile; ricerca web + analisi statistica estesa)

Non essendo arrivati nuovi log (né una cattura UART sull'output della scatoletta, né nuovi candump lato auto), questo giro si è concentrato su (a) ricerca web più approfondita e (b) tecniche statistiche non ancora provate sui due log esistenti (`session_0044.log`, `session_0047.log`): combinazioni a 16 bit su coppie di byte non necessariamente adiacenti, correlazione incrociata gas/freno per ogni candidato, verifica più severa anti-wrap, e analisi dei gap di trasmissione sul bus.

### Ricerca web — risultato negativo ma utile a restringere il campo

Ricerca mirata (repository GitHub con lo stesso schema ID, altre scatolette CAN aftermarket, issue tracker opendbc, forum Prius/Yaris/Aqua, protocollo UART tipico di queste scatolette). Sintesi (fonti complete nella cronologia dell'agente di ricerca):

- **Nessun repository trovato risolve SOC o la formula esatta del carburante per questa esatta piattaforma** (Toyota NoDSU hybrid). Il repo `smartgauges/canbox` (`cars/toyota_premio_26x.c`) conferma che **0xB4 (speed) e 0x611 (odometro) sono ID storici condivisi anche da altre piattaforme Toyota non ibride** (Premio T260, rete BEAN) con formule quasi identiche — ma RPM e marcia sono su ID diversi lì (0x2C4, 0x3B4), e non c'è alcun codice per carburante/batteria in quel file.
- **Verifica diretta sui DBC upstream** (`toyota_nodsu_hybrid_pt_generated.dbc` e varianti correlate, branch `tesla_unity_dev`): confermato che **0x3A0 e 0x4A8 non sono definiti come messaggi in nessun DBC opendbc conosciuto**, e nessun DBC Toyota upstream contiene segnali "FUEL", "SOC" o "BATTERY". Non è quindi un problema già risolto altrove che ci stiamo perdendo: è territorio davvero non documentato.
- **Nessun issue/PR** in `commaai/opendbc` o `BogGyver/opendbc` discute SOC o questi ID specifici.
- **Nessuna scatoletta concorrente** (Raise/SK/Xtrons/Joying — cercate tramite repo firmware open source tipo `smartgauges/canbox`) ha codice di parsing per fuel/SOC su un Toyota ibrido.
- **Dato potenzialmente utile per un futuro tentativo di cattura UART sull'output della scatoletta**: il firmware open-source `smartgauges/canbox` (`canbox.c`) usa un framing seriale `0x2E` (byte di start) + `type` (function id) + `size` + payload + checksum (XOR); esiste anche una variante "hiworld" con framing `0x5A 0xA5 size type payload checksum`. Non è confermato che il Simplesoft RP5-TY-101 usi esattamente questo framing, ma è un punto di partenza plausibile per interpretare un futuro dump UART grezzo (byte di sync `0x2E` o `0x5A 0xA5` da cercare nel flusso).
- **Formula fuel level**: trovato solo lo standard generico SAE OBD-II Mode01 PID 0x2F (`100×A/255`), non specifico Toyota né legato al byte proprietario 0x3A0.

### Verifica quantitativa del candidato carburante (0x3A0 byte[7]) — rafforza l'ipotesi, esclude "autonomia in km"

Prima di questo giro non era stato controllato se il ritmo di diminuzione del byte correlasse meglio con la **distanza percorsa** (che punterebbe a "autonomia residua", uno dei segnali mandatori mancanti) o con il **tempo/motore acceso** (che punterebbe a consumo di carburante reale). Risultato:

- Distanza (da `ODOMETER` 0x611): session_0044 -8 unità / 3 km = **~0.375 km/unità**; session_0047 -6 unità / 1 km = **~0.167 km/unità**. Rapporto fra le due sessioni: **~2.2x di discrepanza**.
- Tempo trascorso (wall-clock): session_0044 ~430s/8 unità = **~53.8 s/unità**; session_0047 ~440s/6 unità = **~73.3 s/unità**. Rapporto: **~1.4x**.
- Tempo motore acceso (RPM>500, integrato fra campioni consecutivi): session_0044 23.4 s/unità; session_0047 38.3 s/unità — **rapporto ~1.6x**, comunque più coerente fra le due sessioni della metrica a distanza (2.2x).
- **Conclusione**: il ritmo di diminuzione segue meglio il tempo/motore-acceso che la distanza percorsa — comportamento atteso per consumo di carburante reale in guida cittadina stop-and-go (molto tempo motore acceso a bassa velocità/fermo, poca distanza), e più difficile da conciliare con un'ipotesi "autonomia residua in km" (che ci si aspetterebbe scalasse più direttamente con la distanza). Il segnale resta comunque coerente con l'ipotesi carburante già in essere; non abbastanza dati per un'esclusione statisticamente forte dell'ipotesi autonomia, ma l'evidenza pende verso carburante.
- Osservazione addizionale: la lettura non decimata del byte (non a campioni ogni 20, ma ogni transizione) mostra un pattern di **oscillazione rapida fra valori adiacenti sovrapposto a un trend lento** (tipico rumore da sciabordio di un galleggiante), non una rampa liscia — ulteriore conferma fisica per "livello carburante analogico filtrato", coerente con l'ipotesi già in essere.
- **Scala/formula in litri resta non confermata**: nessuna fonte trovata online con una formula raw→litri per questo ID proprietario; servirebbe comunque una lettura di riferimento nota (es. subito dopo un pieno).

### Nuovo candidato: secondo segnale di temperatura — `0x3B9 byte[0]` — ⚠️ IPOTESI FORTE (nuovo)

Messaggio a 3 byte (dlc=3, insolito), solo `byte[0]` attivo (byte1/2 sempre 0x00), trasmesso a ~1 Hz. In **entrambi** i log sale in modo liscio e quasi monotono da un valore stabile iniziale fino a un plateau/oscillazione nella parte finale della guida:

- session_0044: 103 → 173 in ~430s, plateau/oscillazione stretta da t≈350s in poi.
- session_0047: 114 → scende brevemente a 110 nei primi 102s → risale fino a 158-160, plateau/oscillazione simile da t≈350-400s.

Correlazione **molto forte con il tempo trascorso** (r=0.945 / r=0.968) **e con il segnale temperatura già confermato** `TEMP_RAW` (0x618 byte[3]) (r=0.950 / r=0.969) — incluso un punto di flesso condiviso (entrambi i segnali rallentano/si stabilizzano nella stessa finestra temporale della sessione), non solo due trend monotoni indipendenti che correlano per coincidenza. Correlazione debole/negativa con l'RPM istantaneo (r=-0.16 / r=-0.10), il che esclude un segnale a risposta rapida e supporta una massa termica più grande/lenta del liquido di raffreddamento (candidati plausibili: olio motore, fluido cambio/CVT, o temperatura del pacco batteria HV — non distinguibili con questi dati). Il valore di plateau differisce fra le due sessioni (173 vs 160), a differenza del liquido di raffreddamento che è regolato a un set-point fisso dal termostato — coerente con una massa termica meno regolata il cui equilibrio dipende dalle condizioni specifiche del tragitto.

Aggiunto al DBC come `TEMP_RAW2` (ID 0x3B9/953). **Nessuna formula/scala proposta.**

### Nuovo candidato esaminato e SCARTATO come SOC: `0x4A2 byte[3]` e `byte[5]`

Durante la scansione a 16-bit è emerso un candidato che a prima vista sembrava molto promettente per un segnale di potenza/corrente della batteria HV: `byte[3]` e `byte[5]` di 0x4A2 mostrano una **forte anti/correlazione con il pedale del freno** (r fino a +0.82/+0.87 per byte5, -0.83/-0.87 per byte3 nei due log) — esattamente il pattern atteso da un segnale "si scarica in accelerazione, si carica in frenata rigenerativa".

**Verifica di controllo (lo stesso tipo di controllo che aveva già smascherato il falso positivo 0x4A8)**: questi due byte correlano ANCORA più fortemente con il segnale **freno analogico già confermato** `BRAKE_ANALOG` (0x224 byte[4:6]): r=-0.93/-0.79 (byte3) e r=+0.93/+0.79 (byte5). Correlano anche con `SPEED` (r=+0.60/+0.22 per byte3), cosa che un segnale di potenza batteria pura non dovrebbe fare (la potenza batteria dipende dall'accelerazione/frenata istantanea, non dalla velocità assoluta del veicolo).

**Conclusione: quasi certamente un secondo segnale legato al freno/decelerazione, proveniente da un'altra ECU (es. ABS/VSC — pressione ai freni per ruota o decelerazione misurata), non un segnale di batteria HV.** Il pattern "carica in frenata" è interamente spiegabile dalla correlazione meccanica con la frenata stessa, che il segnale 0x224 già possiede. Aggiunto al DBC come `CHASSIS_BRAKE2` (ID 0x4A2/1186) con etichetta esplicita "TENTATIVE, non SOC" per evitare di reinvestigarlo in futuro come falso lead.

### Rivalutazione estesa di 0x4A8 (il candidato già ritrattato) — la ritrattazione è confermata con più evidenza

Combinando `byte[1]` (contatore grezzo che sale 0x06→0x09 nel corso della sessione) con i 3 bit alti di `byte[2]` come cifra "fine" dello stesso valore esteso (`byte1*8 + top3(byte2)`), il valore combinato **non è pulito**: sale per lo più ma con diminuzioni locali frequenti e non coerenti con eventi di guida, e soprattutto **occupa una banda diversa e non sovrapposta nei due log** (51-72 in session_0044 contro 50-58 in session_0047, quest'ultimo per giunta con un tuffo verso il basso subito dopo l'inizio). Nessuna narrativa fisica plausibile emerge nemmeno con questa risoluzione estesa. **La ritrattazione resta valida.**

### Scansione sistematica a 16-bit e a sotto-byte, con validazione incrociata rigorosa

Nuovo script `tools/find_word16_candidates.py`: genera ogni combinazione a 16 bit (byte adiacenti big-endian, little-endian, e byte non adiacenti con un salto di 1) per ogni ID non ancora spiegato, con filtro anti-wrap. Nuovo script `tools/cross_validate_candidates.py`: la stessa ricerca ma su **due log contemporaneamente**, tenendo solo i candidati presenti in entrambi con range numerico sovrapposto, più correlazione gas/freno per ciascuno — lo stesso standard di rigore che aveva già smascherato 0x4A8 come falso positivo, applicato sistematicamente invece che caso per caso.

Risultato: **111 combinazioni comuni ai due log** dopo il filtro anti-wrap + range-sovrapposto; **nessuna** ha un profilo compatibile con SOC/EV-mode (bilanciate fra: doppioni del gas pedale/RPM già noti con |r|>0.9, il secondo segnale freno appena descritto e scartato, o telemetria ad alta frequenza/molte transizioni non fisicamente plausibile come batteria HV su una finestra di 7 minuti). Una scansione parallela a livello di nibble/3-bit ("battery bars" in stile function-ID 0x1F di zugetor, 2-8 valori distinti, zero wrap) ha prodotto 92 candidati comuni ai due log, tutti riconducibili a telemetria già nota/rumorosa (famiglie 0x4A0-0x4AF e 0x610-0x61C) senza alcun pattern "a barre lente" plausibile.

Escluso separatamente per lo stesso motivo: `0x63B byte[6]`, che sembrava inizialmente un trend lento interessante ma **si è rivelato un contatore a periodo fisso di ~25.5s** (incrementa di 1 a intervalli quasi perfettamente regolari indipendentemente da marcia/velocità/frenate) — quasi certamente un heartbeat/contatore di uptime, non un segnale fisico. Documentato qui per evitare di reinvestigarlo.

### Nuova osservazione metodologica: gap di trasmissione nel log 0047

Nuovo script `tools/check_bus_gaps.py` (bucket da 10s, segnala bucket con meno dell'80% degli ID unici tipici). In `session_0047.log` è stato trovato un **buco di trasmissione di ~100 secondi** (t≈149.8-211.4s quasi totale, poi recupero parziale 42→91 ID unici fino a t≈250s, poi traffico normale su tutti i 100 ID). Un silenzio bus reale così lungo durante la guida è fisicamente implausibile (SPEED/RPM/GEAR trasmettono comunque in continuo); è molto più probabile un **blocco del buffer di logging dell'ESP32** (throughput sostenuto di ~800 frame/s è tanto per un logger di questo tipo) che un evento CAN genuino o una finestra diagnostica della scatoletta. Documentato come nota di qualità dei dati, non interpretato come indizio SOC — ma utile da monitorare (e possibilmente evitare con storage più veloce) in una futura cattura mirata all'output UART della scatoletta.

### Audit di copertura finale sulla famiglia "combo meter" a bassa frequenza

Controllo manuale mirato sui restanti membri poco campionati della famiglia 0x610-0x615/0x618-0x61c (0x612, 0x613, 0x615, 0x619, 0x61a, 0x61c — ~0.1-0.15 Hz, 34-64 campioni per log, che i filtri statistici automatici (soglie minime su nd/changes) potrebbero aver scartato per numerosità insufficiente):

- `0x613`, `0x615`, `0x61a`: payload quasi/interamente costante in entrambi i log — nessun segnale.
- `0x61c` ultimo byte: contatore puro che incrementa di 1 a ogni messaggio (0,1,2,...) — contatore di sequenza del messaggio, non un valore fisico.
- `0x614`: confermato essere lo stesso `STEERING_LEVERS`/`TURN_SIGNALS` già validato (i payload `29 00 00 10/20/30 ...` corrispondono esattamente ai 3 stati frecce) — non un secondo segnale.
- `0x619 byte[6]`: decresce lentamente (34→31 in session_0044, ~150-175s/unità; 7→6 in session_0047, ~138s/unità — ritmo simile fra i due log) ma il range assoluto è completamente diverso fra le due sessioni (34 vs 7) senza una spiegazione fisica ovvia; troppo pochi campioni (43-44) per un'analisi di correlazione affidabile. Non SOC-shaped (range troppo piccolo e unidirezionale su tutta la sessione, mentre un SOC reale dovrebbe muoversi in entrambe le direzioni con freni/accelerazioni). Lasciato come **osservazione minore non spiegata**, non aggiunto al DBC.
- `0x612 byte[5]`: oscilla in una banda stretta (120-131 in session_0044, 115-129 in session_0047) con variazioni di ±1 ogni ~13s. Controllo di correlazione con tempo/temperatura/velocità/gas: **segno opposto fra i due log** (r=-0.78 col tempo in session_0044, r=+0.94 in session_0047) — lo stesso identico sintomo di instabilità che aveva già smascherato 0x4A8 come falso positivo. Escluso per lo stesso motivo: non è una relazione fisica stabile, è quasi certamente coincidenza statistica su un log corto. Non aggiunto al DBC.

Nessuna di queste voci minori cambia la conclusione principale.

### Conclusione di questo giro

**SOC ed EV-mode restano non trovati.** Rispetto ai giri precedenti, però, il campo di ricerca sul lato auto→scatoletta è ora coperto in modo sistematico e cross-validato (16-bit, sotto-byte, correlazione gas/freno, gap di trasmissione, range diagnostico UDS) su entrambi i log, e la ricerca web conferma che non esiste una soluzione nota altrove per questo esatto schema di ID che ci siamo persi. La conclusione operativa non cambia rispetto al giro precedente ma si rafforza: **il prossimo passo utile non è più statistico sui dati esistenti, ma una nuova acquisizione fisica** — idealmente una cattura logic-analyzer/UART sul lato scatoletta→autoradio (baud rate da provare: 9600/19200/38400 8N1; cercare byte di sincronizzazione `0x2E` o `0x5A 0xA5` come punto di partenza per il framing, per analogia con firmware simili) per osservare direttamente il campo a 3 bit del function ID 0x1F descritto da zugetor, oppure un tentativo di richieste diagnostiche attive (UDS Mode 21/22) verso ECU candidate mentre si ascolta lato auto.

Script aggiunti in questo giro: `tools/find_word16_candidates.py`, `tools/cross_validate_candidates.py`, `tools/check_bus_gaps.py`.
