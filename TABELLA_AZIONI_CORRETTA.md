# ✅ Tabella Corretta — Tutte le Azioni Viessmann

**Base**: REG 101 = Configurazione (bit 14=FREDDO, bit 13=CALDO, bit 7=STANDBY, bit 0-1=ventola)

## Modalità Operative (Acceso vs Spento)

| Azione | Descrizione | Hex (REG101) | Binario | Bit14 | Bit13 | Bit7 | Ventola |
|--------|-------------|-------------|---------|-------|-------|------|---------|
| **CALDO ON** | Riscaldamento acceso | 0x2000-0x2003 | 0010 0000 0000-0011 | 0 | 1 | 0 | 0-3 |
| **FREDDO ON** | Raffrescamento acceso | 0x4000-0x4003 | 0100 0000 0000-0011 | 1 | 0 | 0 | 0-3 |
| **CALDO OFF** | Riscaldamento spento | 0x2080-0x2083 | 0010 0000 1000-1011 | 0 | 1 | 1 | 0-3 |
| **FREDDO OFF** | Raffrescamento spento | 0x4080-0x4083 | 0100 0000 1000-1011 | 1 | 0 | 1 | 0-3 |

## Esempi Pratici Completi

### Riscaldamento (CALDO)

| Scenario | Reg 101 | Dettagli |
|----------|---------|----------|
| Caldo ON, ventola AUTO | 0x2000 | bit13=1, bit0-1=00 |
| Caldo ON, ventola MIN | 0x2001 | bit13=1, bit0-1=01 |
| Caldo ON, ventola NIGHT | 0x2002 | bit13=1, bit0-1=10 |
| Caldo ON, ventola MAX | 0x2003 | bit13=1, bit0-1=11 |
| **Caldo STANDBY** | **0x2083** | bit13=1, bit7=1 (LED spenti) |

### Raffrescamento (FREDDO)

| Scenario | Reg 101 | Dettagli |
|----------|---------|----------|
| Freddo ON, ventola AUTO | 0x4000 | bit14=1, bit0-1=00 |
| Freddo ON, ventola MIN | 0x4001 | bit14=1, bit0-1=01 |
| Freddo ON, ventola NIGHT | 0x4002 | bit14=1, bit0-1=10 |
| Freddo ON, ventola MAX | 0x4003 | bit14=1, bit0-1=11 |
| **Freddo STANDBY** | **0x4083** | bit14=1, bit7=1 (LED spenti) |

## Operazioni di Controllo (velocità ventola)

| Comando | Reg 101 | Bit 0-1 | Note |
|---------|---------|--------|------|
| **FAN AUTO** | +0x00 | 00 | Velocità automatica |
| **FAN MIN** | +0x01 | 01 | Velocità minima |
| **FAN NIGHT** | +0x02 | 10 | Velocità notturna |
| **FAN MAX** | +0x03 | 11 | Velocità massima |

## Tabella Semplificata — Azioni Principali

| Azione | Comando | Reg 101 | Bin | Bit14 | Bit13 | Bit7 |
|--------|---------|---------|-----|-------|-------|------|
| ✅ Accendi riscaldamento | HEAT | 0x2003 | 0010 0000 0011 | 0 | 1 | 0 |
| ✅ Accendi raffrescamento | COOL | 0x4003 | 0100 0000 0011 | 1 | 0 | 0 |
| ❌ Spegni | OFF | 0x2083 o 0x4083 | +bit7 | ✓ | ✓ | 1 |
| 🌡️ Regola ventola | FAN[0-3] | bit 0-1 | - | - | - | - |

## Come è costruito il valore 0xHHLL

```
Bit 15-8: HH (byte alto)
Bit 7-0:  LL (byte basso)

Esempio 0x2003:
  Bit 14 (CALDO)   = 1 → byte alto = 0x20 (00100000)
  Bit 0-1 (FAN=11) = 3 → byte basso = 0x03 (00000011)
  Risultato: 0x2003 ✓

Esempio 0x4083 (FREDDO + STANDBY):
  Bit 14 (FREDDO)  = 1 → byte alto = 0x40 (01000000)
  Bit 7 (STANDBY)  = 1 → byte alto = 0x80 (aggiunge 80)
  Bit 0-1 (FAN=11) = 3 → byte basso = 0x03 (00000011)
  Risultato: 0x4083 ✓
```

## Errori nella tabella originale

| Errore | Originale | Corretto |
|--------|-----------|----------|
| ❌ ACCENDI e FREDDO duplicati (0x4003) | ACCENDI=0x4003, FREDDO=0x4003 | ACCENDI è un'azione generica, usa CALDO(0x2003) o FREDDO(0x4003) |
| ❌ Mancanza di STANDBY | Solo ON/OFF come azioni | Aggiungere bit7 (0x80) per spegnimento |
| ❌ FAN OFF/MIN/AUTO/MAX confusi | execute(0,1,2,3) senza chiarezza | Sono variazioni di bit 0-1, non azioni separate |
| ❌ Binario sbagliato per SPEGNI | 01000000 1011 | Corretto: 01000000 1011 (ma era usato per FREDDO, non SPEGNI alone) |

---

**Conclusione**: La tabella originale mischiava concetti. Quella corretta separa:
- **Modalità** (CALDO/FREDDO) → bit 13/14
- **Stato** (ON/OFF) → bit 7
- **Velocità ventola** → bit 0-1
