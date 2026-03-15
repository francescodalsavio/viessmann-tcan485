# VISLA Modbus Controller — Viessmann Energycal Slim W

Controllo remoto dei ventilconvettori Viessmann Energycal Slim W tramite LilyGo T-CAN485 (ESP32) via Modbus ASCII su RS485.

## Come funziona

Il comando touch originale Viessmann comunica con la scheda motore del ventilconvettore tramite **Modbus ASCII** (non RTU) sul bus RS485 (morsetti A/B).

Il T-CAN485 sostituisce il comando touch come master del bus e invia gli stessi comandi:
- **Write Single Register (0x06)** in broadcast (indirizzo 0)
- **3 registri** inviati ogni 10 secondi
- **Baudrate**: 9600, 8N1

## Collegamento fisico

```
LilyGo T-CAN485          Ventilconvettore (morsetti sul retro del comando)
┌──────────┐              ┌──────────────┐
│  RS485   │              │   A  (bianco) ◄── filo bianco
│  [A] ────│── bianco ───│              │
│  [GND]   │  (lasciare  │              │
│  [B] ────│── marrone ──│   B (marrone) ◄── filo marrone
│          │   vuoto)     │              │
│  USB-C ◄─│── 5V        └──────────────┘
└──────────┘
```

**Importante**: Scollegare i fili A/B dal comando touch prima di collegare il T-CAN485. Il comando touch va scollegato perche' e' anche lui un master e sovrascrive i comandi.

Sulla morsettiera RS485 del T-CAN485 ci sono 3 viti: **A**, **GND** (centrale), **B**. Usare solo A e B, lasciare GND vuoto.

## Registri Modbus

| Registro | Funzione | Valori |
|----------|----------|--------|
| **101** | Configurazione | Bit 14: ON(1)/OFF(0). Bit 0-1: velocita' ventola |
| **102** | Temperatura setpoint | Valore × 10 (es. 205 = 20.5°C) |
| **103** | Modo stagionale | 0x0082 = caldo, 0x0080 = freddo |

### Registro 101 — Dettaglio bit

| Bit | Funzione | Valori |
|-----|----------|--------|
| 0-1 | Velocita' ventola | 0=AUTO, 1=MIN, 2=NIGHT, 3=MAX |
| 14 | Acceso/Spento | 0=spento, 1=acceso |

Esempi:
- `0x4003` = acceso + ventola MAX
- `0x4000` = acceso + ventola AUTO
- `0x0003` = spento + ventola MAX

### Registro 103 — Modo stagionale

| Valore | Significato |
|--------|-------------|
| `0x0082` | Riscaldamento (caldo) |
| `0x0080` | Raffrescamento (freddo) |

Il bit 1 controlla caldo(1)/freddo(0).

## Comandi via seriale (USB)

Collegare il T-CAN485 al computer via USB-C e aprire il monitor seriale a **115200 baud**.

| Comando | Funzione | Esempio |
|---------|----------|---------|
| `T<valore>` | Imposta temperatura (5-35°C) | `T22.5` → 22.5°C |
| `ON` | Accendi ventilconvettore | |
| `OFF` | Spegni ventilconvettore | |
| `FAN0` | Ventola AUTO | |
| `FAN1` | Ventola MIN | |
| `FAN2` | Ventola NIGHT | |
| `FAN3` | Ventola MAX | |
| `HEAT` | Modo riscaldamento | |
| `COOL` | Modo raffrescamento | |
| `STATUS` | Mostra stato attuale | |
| `SEND` | Forza invio immediato dei registri | |
| `HELP` | Mostra lista comandi | |

## Compilazione e flash

Requisiti: [PlatformIO](https://platformio.org/)

```bash
cd visla-modbus

# Compila
python3 -m platformio run

# Compila e flasha
python3 -m platformio run --target upload

# Monitor seriale
python3 -m platformio device monitor
```

La porta seriale e' configurata in `platformio.ini`. Se cambia, aggiornare `upload_port` e `monitor_port`.

## Protocollo Modbus ASCII

A differenza del Modbus RTU (binario), il Modbus ASCII trasmette ogni byte come 2 caratteri esadecimali ASCII.

Formato frame:
```
: AA FF DD DD DD DD ... CC CR LF
│ │  │  │              │  │
│ │  │  │              │  └─ Fine frame (\r\n)
│ │  │  │              └──── LRC checksum (1 byte hex)
│ │  │  └─────────────────── Dati (N byte hex)
│ │  └────────────────────── Function code (1 byte hex)
│ └───────────────────────── Indirizzo slave (1 byte hex)
└─────────────────────────── Inizio frame (':')
```

Esempio — il comando touch invia:
```
:00060065400352\r\n
 │ │  │    │   └── LRC
 │ │  │    └────── Valore: 0x4003 (acceso, ventola MAX)
 │ │  └─────────── Registro: 101 (0x0065)
 │ └───────────── Funzione: 0x06 (Write Single Register)
 └─────────────── Indirizzo: 0 (broadcast)
```

## Note tecniche

- Il comando touch trasmette i 3 registri ogni ~67 secondi
- Il T-CAN485 li invia ogni 10 secondi per maggiore reattivita'
- Il transceiver RS485 (MAX13487E) ha auto-direction, non serve gestire DE/RE manualmente
- I dati vengono letti come 8N1 e il bit alto viene strippato (il dispositivo usa 7E1)

## Tutte le combinazioni controllabili

| Parametro | Valori possibili | Registro |
|-----------|-----------------|----------|
| **ON/OFF** | Acceso / Spento | Reg 101, bit 14 |
| **Ventola** | AUTO, MIN, NIGHT, MAX | Reg 101, bit 0-1 |
| **Temperatura** | 5.0°C — 35.0°C (passi da 0.1°C) | Reg 102 |
| **Stagione** | Caldo / Freddo | Reg 103, bit 1 |

In totale: 2 (on/off) × 4 (ventola) × 301 (temperature) × 2 (caldo/freddo) = **4816 combinazioni**.

### Esempi pratici

| Scenario | Comandi |
|----------|---------|
| Inverno, riscaldamento forte | `ON` `HEAT` `T25` `FAN3` |
| Inverno, riscaldamento leggero notte | `ON` `HEAT` `T20` `FAN2` |
| Estate, raffrescamento forte | `ON` `COOL` `T18` `FAN3` |
| Estate, raffrescamento leggero | `ON` `COOL` `T22` `FAN1` |
| Spento | `OFF` |

### Valori registri per ogni scenario

| Scenario | Reg 101 | Reg 102 | Reg 103 |
|----------|---------|---------|---------|
| Acceso, caldo, 25°C, ventola MAX | `0x4003` | `0x00FA` (250) | `0x0082` |
| Acceso, caldo, 20°C, ventola NIGHT | `0x4002` | `0x00C8` (200) | `0x0082` |
| Acceso, freddo, 18°C, ventola MAX | `0x4003` | `0x00B4` (180) | `0x0080` |
| Acceso, freddo, 22°C, ventola MIN | `0x4001` | `0x00DC` (220) | `0x0080` |
| Acceso, caldo, 22°C, ventola AUTO | `0x4000` | `0x00DC` (220) | `0x0082` |
| Spento | `0x0003` | (invariato) | (invariato) |

## Come sono stati scoperti i registri

### Passo 1 — Primo scan Modbus RTU (fallito)

Scan di tutti gli indirizzi 1-247 con Modbus RTU (binario). Nessuna risposta diretta dai ventilconvettori, ma dati "spuri" catturati su alcuni indirizzi durante lo scan.

### Passo 2 — Riconoscimento protocollo Modbus ASCII

I dati ricevuti iniziavano con `0x3A` (`:` in ASCII) e finivano con `0x8D 0x0A`. Questo ha rivelato che:
- `:` = inizio frame **Modbus ASCII** (non RTU)
- `0x8D` = `0x0D` (CR) con bit 7 alto, perche' il dispositivo usa **7E1** (7 bit dati + parita' pari) ma noi leggevamo con 8N1

### Passo 3 — Decodifica del traffico

Strippando il bit 7 da ogni byte, i frame sono diventati leggibili:

```
Frame originale (8N1): 3A 30 30 30 36 30 30 36 35 B4 30 30 33 35 B2 8D 0A
Dopo strip bit 7:      :  0  0  0  6  0  0  6  5  4  0  0  3  5  2  CR LF
Frame decodificato:     :00060065400352
```

Decodifica Modbus ASCII:
- Indirizzo `00` = broadcast
- Funzione `06` = Write Single Register
- Registro `0065` = 101
- Valore `4003` = configurazione
- LRC `52` = checksum verificato OK

### Passo 4 — Ascolto passivo del bus (90 secondi)

Il comando touch trasmette autonomamente ogni ~67 secondi, sempre 3 frame:

```
:00060065400352   → Reg 101 = 0x4003 (config: acceso, ventola MAX)
:0006006600CDC7   → Reg 102 = 0x00CD = 205 (temperatura: 20.5°C ← confermato dal display!)
:00060067008211   → Reg 103 = 0x0082 (modo: riscaldamento)
```

### Passo 5 — Interpretazione dei valori

- **Reg 102**: valore `0x00CD` = 205. Il display del touch mostrava 20.5°C → quindi il valore = temperatura × 10
- **Reg 101**: valore `0x4003` = `0100 0000 0000 0011` in binario. Bit 14=1 (acceso), bit 0-1=11 (ventola MAX). Coerente con documentazione di ventilconvettori simili
- **Reg 103**: `0x0082` vs `0x0080` — il bit 1 distingue caldo/freddo (da documentazione e test)

### Passo 6 — Verifica che il touch sovrascrive

Con il comando touch ancora collegato, abbiamo provato a scrivere 21.5°C. Il touch ha riscritto 20.5°C al ciclo successivo (~67 sec dopo). Soluzione: scollegare il touch dal bus.

### Passo 7 — Test fisico (conferma definitiva)

Con il comando touch scollegato:
1. Inviato `OFF` → la ventola si e' fermata ✓
2. Inviato `ON` → la ventola e' ripartita ✓
3. Inviato `T22.5` → setpoint cambiato ✓
4. Inviato `COOL` → modo raffrescamento ✓

Tutto confermato funzionante.

## TODO

- [ ] Aggiungere WiFi con API REST per controllo da browser/telefono
- [ ] Estendere a tutti e 5 i ventilconvettori
- [ ] Integrazione con backend VISLA
- [ ] Geofencing GPS (accendi quando arrivi a casa)
- [ ] Programmazione oraria (mattina, sera, notte, weekend)
