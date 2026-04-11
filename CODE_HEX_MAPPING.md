# Code Hex Mapping — Dove Sono i Valori nel Codice

Guida di come i valori hex/binario sono usati nel codice per ogni azione.

---

## 🟢 ACCENDI (Power ON - Smart)

### File: `src/domain/use_cases_v2.h` — TogglePowerUseCase::executePowerOn()

```cpp
void executePowerOn(uint16_t currentRegConfig) {
  // ⭐ SMART: Mantiene la modalità attuale (CALDO o FREDDO)
  uint16_t regConfig = currentRegConfig;
  regConfig &= ~(1 << 7);  // ← Spegni bit 7 (STANDBY) per accendere
  // Il resto della configurazione (bit 14, 13, 0-1) rimane invariato

  Result<void> result = repository.sendRegister(101, regConfig);
}
```

### Breakdown Hex

```
SCENARIO 1: Se era in CALDO
  Prima:  0x2083 (CALDO + STANDBY + FAN MAX)
  Dopo:   0x2003 (CALDO + ON + FAN MAX) ← bit7 da 1 a 0

SCENARIO 2: Se era in FREDDO
  Prima:  0x4083 (FREDDO + STANDBY + FAN MAX)
  Dopo:   0x4003 (FREDDO + ON + FAN MAX) ← bit7 da 1 a 0

Operazione bitwise:
  regConfig &= ~(1 << 7);  // Cancella il bit 7
  // Mantiene bit14, bit13, bit0-1 invariati
```

### Cosa Significa
- **ACCENDI è modalità-agnostico**: se eri in CALDO, rimani in CALDO
- **Operazione**: spegni il bit 7 (STANDBY) della configurazione attuale
- **Vantaggio**: non costringe l'utente a FREDDO, mantiene la preferenza precedente
- **Temperatura**: non cambia (non tocchiamo REG 102)

---

## 🔴 SPEGNI (Power OFF - Smart)

### File: `src/domain/use_cases_v2.h` — TogglePowerUseCase::executePowerOff()

```cpp
void executePowerOff(uint16_t currentRegConfig) {
  // ⭐ SMART: Mantiene la modalità attuale (CALDO o FREDDO)
  uint16_t regConfig = currentRegConfig;
  regConfig |= (1 << 7);  // ← Accendi bit 7 (STANDBY) per spegnere
  // Il resto della configurazione (bit 14, 13, 0-1) rimane invariato

  Result<void> result = repository.sendRegister(101, regConfig);
}
```

### Breakdown Hex

```
SCENARIO 1: Se era in CALDO
  Prima:  0x2003 (CALDO + ON + FAN MAX)
  Dopo:   0x2083 (CALDO + STANDBY + FAN MAX) ← bit7 da 0 a 1

SCENARIO 2: Se era in FREDDO
  Prima:  0x4003 (FREDDO + ON + FAN MAX)
  Dopo:   0x4083 (FREDDO + STANDBY + FAN MAX) ← bit7 da 0 a 1

Operazione bitwise:
  regConfig |= (1 << 7);  // Accendi il bit 7
  // Mantiene bit14, bit13, bit0-1 invariati
```

### Cosa Significa
- **SPEGNI è modalità-agnostico**: se eri in CALDO, rimani in CALDO (ma spento)
- **Operazione**: accendi il bit 7 (STANDBY) della configurazione attuale
- **Vantaggio**: spegne solo il ventilconvettore, non lo forza a una modalità
- **Temperatura**: non cambia (non tocchiamo REG 102)

---

## 🔥 CALDO (Heating Mode)

### File: `src/domain/use_cases_v2.h` — ChangeHeatingModeUseCase::executeHeating()

```cpp
void executeHeating(uint16_t currentRegConfig) {
  uint16_t regConfig = currentRegConfig;
  regConfig &= ~(1 << 14);  // ← Spegni bit 14 (FREDDO)
  regConfig |= (1 << 13);   // ← Accendi bit 13 (CALDO)

  Result<void> result = repository.sendRegister(101, regConfig);
}
```

### Bit Operations

```
Prima (FREDDO):
  Binario: 0100 0000 0000 0011 = 0x4003
           └─ bit14=1, bit13=0

Dopo (CALDO):
  Binario: 0010 0000 0000 0011 = 0x2003
           └─ bit14=0, bit13=1 ← FLIPPED!

Risultato: 0x2003 = CALDO MAX
```

### C++ Bitwise Operations

```cpp
// Spegni bit 14 (FREDDO)
regConfig &= ~(1 << 14);  // Cancella il bit 14

// Accendi bit 13 (CALDO)
regConfig |= (1 << 13);   // Setta il bit 13

// OPPURE in forma compatta:
regConfig = (regConfig & ~0x4000) | 0x2000;
```

### Cosa Significa
- **bit14=0, bit13=1** = CALDO ✓
- **0x2003** = CALDO MAX

---

## ❄️ FREDDO (Cooling Mode)

### File: `src/domain/use_cases_v2.h` — ChangeHeatingModeUseCase::executeCooling()

```cpp
void executeCooling(uint16_t currentRegConfig) {
  uint16_t regConfig = currentRegConfig;
  regConfig &= ~(1 << 13);  // ← Spegni bit 13 (CALDO)
  regConfig |= (1 << 14);   // ← Accendi bit 14 (FREDDO)

  Result<void> result = repository.sendRegister(101, regConfig);
}
```

### Bit Operations

```
Prima (CALDO):
  Binario: 0010 0000 0000 0011 = 0x2003
           └─ bit14=0, bit13=1

Dopo (FREDDO):
  Binario: 0100 0000 0000 0011 = 0x4003
           └─ bit14=1, bit13=0 ← FLIPPED!

Risultato: 0x4003 = FREDDO MAX (default)
```

### Cosa Significa
- **bit14=1, bit13=0** = FREDDO ✓
- **0x4003** = FREDDO MAX (modalità default)

---

## 🌡️ TEMPERATURA (Temperature Setpoint)

### File: `src/domain/use_cases_v2.h` — SetTemperatureUseCase::execute()

```cpp
void execute(float temperature) {
  // Validazione
  if (temperature < 5.0f) temperature = 5.0f;     // Min
  if (temperature > 24.0f) temperature = 24.0f;   // Max

  // Conversione a hex
  uint16_t regValue = (uint16_t)(temperature * 10);
  // ↑ FORMULA: Celsius × 10 = hex value

  Result<void> result = repository.sendRegister(102, regValue);
}
```

### Esempi Conversione Temperatura

```
Celsius → Decimal → Hex
5.0°C   → 5×10=50    → 0x32
10.0°C  → 10×10=100  → 0x64
16.0°C  → 16×10=160  → 0xA0
20.0°C  → 20×10=200  → 0xC8
20.5°C  → 20.5×10=205 → 0xCD ← ACCENDI default
22.5°C  → 22.5×10=225 → 0xE1
24.0°C  → 24×10=240  → 0xF0 ← MAX firmware
```

### Range Validazione

```cpp
if (temperature < 5.0f)    // Min: 0x32
if (temperature > 24.0f)   // Max: 0xF0
```

### Cosa Significa
- **Formula: Celsius × 10 = Valore Hex**
- **REG 102** contiene il setpoint temperature
- **Range: 5.0 - 24.0°C** nel firmware nostro
- **Incrementi: 0.5°C per ogni +5 in hex**

---

## 🌀 VENTOLA (Fan Speed)

### File: `src/domain/use_cases_v2.h` — ChangeFanSpeedUseCase::execute()

```cpp
void execute(int speed, uint16_t currentRegConfig) {
  if (speed < 0 || speed > 3) {
    logger->error("[UC] Invalid fan speed");
    return;
  }

  uint16_t regConfig = currentRegConfig;
  regConfig = (regConfig & ~0x03) | (speed & 0x03);
  // ↑ Modifica solo bit 1-0, mantieni il resto

  Result<void> result = repository.sendRegister(101, regConfig);
}
```

### Bit Operations - Fan Speed (bit 1-0)

```
Speed  Binario  Hex  Significato
  0      00    0x00  OFF
  1      01    0x01  MIN
  2      10    0x02  AUTO/NIGHT
  3      11    0x03  MAX ← Default

Combinazioni con FREDDO:
  Speed 0: 0x4000 (FREDDO + FAN OFF)
  Speed 1: 0x4001 (FREDDO + FAN MIN)
  Speed 2: 0x4002 (FREDDO + FAN AUTO)
  Speed 3: 0x4003 (FREDDO + FAN MAX) ✓

Combinazioni con CALDO:
  Speed 0: 0x2000 (CALDO + FAN OFF)
  Speed 1: 0x2001 (CALDO + FAN MIN)
  Speed 2: 0x2002 (CALDO + FAN AUTO)
  Speed 3: 0x2003 (CALDO + FAN MAX)
```

### C++ Bitwise Operations

```cpp
// Mascherare i bit 1-0 (cancella speedOldBits, setta speedNewBits)
regConfig = (regConfig & ~0x03) | (speed & 0x03);
//          └─ Cancella     └─ Setta nuovo

// Oppure manualmente per clarity:
if (speed == 0) regConfig = (regConfig & ~0x03) | 0x00;  // OFF
if (speed == 1) regConfig = (regConfig & ~0x03) | 0x01;  // MIN
if (speed == 2) regConfig = (regConfig & ~0x03) | 0x02;  // AUTO
if (speed == 3) regConfig = (regConfig & ~0x03) | 0x03;  // MAX
```

### Cosa Significa
- **Bit 1-0** = velocità ventola
- **00** = OFF, **01** = MIN, **10** = AUTO, **11** = MAX
- **Default: 3 (MAX)**
- **Range: 0-3**

---

## 📋 Tabella Veloce — Tutte le Azioni

| Azione | Operazione | Hex Risultato | Bitwise Op | Note |
|--------|-----------|---------------|-----------|------|
| **ACCENDI** | executePowerOn | Dipende da stato | `regConfig &= ~(1<<7)` | ⭐ Smart: mantiene CALDO/FREDDO |
| **SPEGNI** | executePowerOff | Dipende da stato | `regConfig \|= (1<<7)` | ⭐ Smart: mantiene CALDO/FREDDO |
| **CALDO** | executeHeating | 0x2000-0x2003 | `regConfig &= ~(1<<14); regConfig \|= (1<<13);` | Accende bit13, spegne bit14 |
| **FREDDO** | executeCooling | 0x4000-0x4003 | `regConfig &= ~(1<<13); regConfig \|= (1<<14);` | Accende bit14, spegne bit13 |
| **TEMP** | SetTemperatureUseCase | 0x32-0xF0 | `(temp * 10)` | Formula: Celsius × 10 |
| **FAN OFF** | execute(0,...) | Dipende da modo | `regConfig = (regConfig & ~0x03) \| 0x00` | bit0-1 = 00 (OFF) |
| **FAN MIN** | execute(1,...) | Dipende da modo | `regConfig = (regConfig & ~0x03) \| 0x01` | bit0-1 = 01 (MIN) |
| **FAN AUTO** | execute(2,...) | Dipende da modo | `regConfig = (regConfig & ~0x03) \| 0x02` | bit0-1 = 10 (AUTO/NIGHT) |
| **FAN MAX** | execute(3,...) | Dipende da modo | `regConfig = (regConfig & ~0x03) \| 0x03` | bit0-1 = 11 (MAX) |

### Esempi di Risultati dopo ACCENDI/SPEGNI

```
Se è in CALDO (0x2003):
  ACCENDI: 0x2003 (non cambia, bit7 era già 0)
  SPEGNI:  0x2083 (aggiunge bit7)

Se è in FREDDO (0x4003):
  ACCENDI: 0x4003 (non cambia, bit7 era già 0)
  SPEGNI:  0x4083 (aggiunge bit7)

Se è in CALDO+STANDBY (0x2083):
  ACCENDI: 0x2003 (toglie bit7)
  SPEGNI:  0x2083 (non cambia, bit7 era già 1)
```

---

## 🔍 Come Leggere il Codice

### Pattern Ricorrente

```cpp
// 1. DICHIARAZIONE
uint16_t regValue = 0x4003;  // ← Hex value

// 2. MODIFICA (con bitwise operations)
regValue &= ~(1 << 14);      // Spegni bit 14
regValue |= (1 << 13);       // Accendi bit 13

// 3. INVIO
Result<void> result = repository.sendRegister(101, regValue);

// 4. VERIFICA
if (result.isOk()) {
  logger->info("Success");
} else {
  logger->error(result.errorMessage());
}
```

### Conversion Formula

```
Celsius        →  Decimal      →  Hex
22.5°C         →  225 (22.5×10) → 0xE1
```

---

## 📁 File Principali con Valori Hex

1. **`src/domain/use_cases_v2.h`**
   - SetTemperatureUseCase (TEMP formula)
   - TogglePowerUseCase (ON/OFF hex)
   - ChangeHeatingModeUseCase (CALDO/FREDDO bitwise)
   - ChangeFanSpeedUseCase (FAN bitwise)

2. **`src/test/mock_repository.h`**
   - Verifica valori hex negli assert

3. **`src/test/test_viessmann.cpp`**
   - Test per ogni azione
   - Assertion sui valori attesi

4. **`MODBUS_PROTOCOL.md`**
   - Tabella completa REG 101 (16 combinazioni)
   - Tabella temperature (5-40°C)
   - Bit mapping dettagliato

---

**Ultimo aggiornamento:** Aprile 2026
