# 📊 Confronto HEX — Prima vs Dopo

## Riepilogo: Cosa è Cambiato

**La chiave**: ACCENDI e SPEGNI **non erano hardcoded**, ma **mantengono la modalità corrente** (CALDO/FREDDO).

---

## 🔄 Scenario 1: Utente in CALDO (0x2003)

### Se premi ACCENDI

| Aspetto | PRIMA | DOPO | Cambio |
|---------|-------|------|--------|
| Hex inviato | **0x4003** ❌ | **0x2003** ✅ | Saltava a FREDDO |
| Cosa fa | Forza FREDDO MAX | Rimane CALDO MAX | ⭐ Corretto |
| Bit 14 | 1 (FREDDO) | 0 (no FREDDO) | Spento |
| Bit 13 | 0 | 1 (CALDO) | Acceso |
| Bit 7 | 0 (ON) | 0 (ON) | Invariato |

**Conclusione**: Prima ti forzava a FREDDO, ora rimani in CALDO ✅

---

### Se premi SPEGNI

| Aspetto | PRIMA | DOPO | Cambio |
|---------|-------|------|--------|
| Hex inviato | **0x4083** ❌ | **0x2083** ✅ | Modalità sbagliata |
| Cosa fa | FREDDO + STANDBY | CALDO + STANDBY | ⭐ Corretto |
| Bit 14 | 1 (FREDDO) | 0 (no FREDDO) | Spento |
| Bit 13 | 0 | 1 (CALDO) | Acceso |
| Bit 7 | 1 (OFF) | 1 (OFF) | Invariato |

**Conclusione**: Prima spegneva ma forzava FREDDO, ora rimani in CALDO spento ✅

---

### Se cambi velocità ventola (FAN)

| Azione | PRIMA | DOPO | Cambio |
|--------|-------|------|--------|
| FAN OFF | 0x4000 ❌ | 0x2000 ✅ | Era FREDDO, ora CALDO |
| FAN MIN | 0x4001 ❌ | 0x2001 ✅ | Era FREDDO, ora CALDO |
| FAN AUTO | 0x4002 ❌ | 0x2002 ✅ | Era FREDDO, ora CALDO |
| FAN MAX | 0x4003 ❌ | 0x2003 ✅ | Invariato |

**Conclusione**: FAN si adatta alla modalità corrente, non forza FREDDO ✅

---

## 🔄 Scenario 2: Utente in FREDDO (0x4003)

### Se premi ACCENDI

| Aspetto | PRIMA | DOPO | Cambio |
|---------|-------|------|--------|
| Hex inviato | **0x4003** ✅ | **0x4003** ✅ | Nessun cambio |
| Cosa fa | FREDDO MAX ON | FREDDO MAX ON | ⭐ Stesso risultato |
| Bit 14 | 1 (FREDDO) | 1 (FREDDO) | Invariato |
| Bit 13 | 0 | 0 | Invariato |
| Bit 7 | 0 (ON) | 0 (ON) | Invariato |

**Conclusione**: In FREDDO era già corretto, rimane uguale ✅

---

### Se premi SPEGNI

| Aspetto | PRIMA | DOPO | Cambio |
|---------|-------|------|--------|
| Hex inviato | **0x4083** ✅ | **0x4083** ✅ | Nessun cambio |
| Cosa fa | FREDDO + STANDBY | FREDDO + STANDBY | ⭐ Stesso risultato |
| Bit 14 | 1 (FREDDO) | 1 (FREDDO) | Invariato |
| Bit 13 | 0 | 0 | Invariato |
| Bit 7 | 1 (OFF) | 1 (OFF) | Invariato |

**Conclusione**: In FREDDO era già corretto, rimane uguale ✅

---

### Se cambi velocità ventola (FAN)

| Azione | PRIMA | DOPO | Cambio |
|--------|-------|------|--------|
| FAN OFF | 0x4000 ✅ | 0x4000 ✅ | Nessun cambio |
| FAN MIN | 0x4001 ✅ | 0x4001 ✅ | Nessun cambio |
| FAN AUTO | 0x4002 ✅ | 0x4002 ✅ | Nessun cambio |
| FAN MAX | 0x4003 ✅ | 0x4003 ✅ | Nessun cambio |

**Conclusione**: In FREDDO era già corretto, rimane uguale ✅

---

## 📋 Riepilogo Totale

### Hex che **CAMBIAVANO** se eri in CALDO

```
PRIMA (SBAGLIATO):
  ACCENDI → 0x4003 (salta a FREDDO!) ❌
  SPEGNI  → 0x4083 (FREDDO spento!) ❌
  FAN OFF → 0x4000 (FREDDO!) ❌
  FAN MIN → 0x4001 (FREDDO!) ❌
  FAN AUTO → 0x4002 (FREDDO!) ❌

DOPO (CORRETTO):
  ACCENDI → 0x2003 (rimane CALDO) ✅
  SPEGNI  → 0x2083 (CALDO spento) ✅
  FAN OFF → 0x2000 (CALDO) ✅
  FAN MIN → 0x2001 (CALDO) ✅
  FAN AUTO → 0x2002 (CALDO) ✅
```

### Hex che **RIMANEVANO UGUALI** se eri in FREDDO

```
PRIMA e DOPO (IDENTICO):
  ACCENDI → 0x4003
  SPEGNI  → 0x4083
  FAN OFF → 0x4000
  FAN MIN → 0x4001
  FAN AUTO → 0x4002
  FAN MAX → 0x4003
```

---

## 🎯 Conclusione

| Condizione | Hex Modificati | Impatto |
|------------|----------------|--------|
| **Eri in CALDO** | 5 hex cambiati (ACCENDI, SPEGNI, FAN 0-2) | 🔴 CRITICO: Prima forzava FREDDO |
| **Eri in FREDDO** | 0 hex cambiati | ✅ Nessun impatto |
| **Globale** | Correzione logica, non numerica | ⭐ Smart context-aware |

---

## 💡 Il Vero Cambiamento

Non sono i valori hex a cambiare, ma **la logica di quando inviarli**:

**PRIMA**:
- `ACCENDI()` → invia sempre 0x4003
- `SPEGNI()` → invia sempre 0x4083

**DOPO**:
- `ACCENDI()` → leggi stato attuale, toggli bit7, reinvia
- `SPEGNI()` → leggi stato attuale, accendi bit7, reinvia

**Conseguenza**: L'hex che viene inviato dipende da dove eri prima, non è hardcoded.
