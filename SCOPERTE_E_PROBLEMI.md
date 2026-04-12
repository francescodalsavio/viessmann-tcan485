# VISLA TCAN485 - Scoperte e Problemi Risolti

## 🔍 Scoperte Principali

### 1. **Indirizzo Modbus = BROADCAST (0x00)**
- ✅ Il Master Viessmann originale invia a **indirizzo 0x00 (broadcast)**
- ✅ **TUTTI e 3 i ventilconvettori ricevono lo STESSO comando**
- ❌ Non è necessario mandare a indirizzi separati (1, 2, 3)
- **Fonte:** Sniffed da termostato originale (vedere sniffer_view.h)

### 2. **Baud Rate Critico: 19200 > 9600**
| Baud Rate | Risultato | Problema |
|-----------|-----------|----------|
| 9600 | Accende solo 1 ventilo | TOO SLOW - i frame non sincronizzano |
| 19200 | Accende tutti e 3 | ✓ Perfetto - sincronizzazione veloce |
| 38400 | Non testato | Probabilmente troppo veloce |

**Motivo:**
- Con 9600 baud, la trasmissione di 3 frame Modbus è lenta
- I boiler potrebbero timeout o perdere frame
- Con 19200, i frame arrivano abbastanza veloce da sincronizzarsi

### 3. **REG 103 (Modo Stagionale) = 0xAF (non 0x008A, non 0xB9)**
- ✅ Valore corretto: **0xAF**
- ❌ Sbagliato: 0x008A, 0xB9
- **Fonte:** Sniffed dal Master Viessmann originale (JSON sniffer)

### 4. **Formato Frame Modbus ASCII**
```
:AAFFRRRRRRRRLLCC\r\n

AA = Indirizzo slave (0x00 = broadcast)
FF = Funzione (0x06 = Write Single Register)
RRRR = Numero registro (0065=REG101, 0066=REG102, 0067=REG103)
RRRR = Valore (0x4003=FREDDO ACCESO MAX, 0x4083=FREDDO SPENTO MAX)
LL = Checksum LRC
CC = Carriage return + Line feed
```

### 5. **Delay tra Frame = 1.15-1.16 secondi**
- Frame 1 (REG 101): t=0.00s
- Frame 2 (REG 102): t=1.16s (delay +1.16s)
- Frame 3 (REG 103): t=2.31s (delay +1.15s)
- **Nota:** Delay anomali a volte 2.40s, 1.97s tra frame non consecutivi

### 6. **Sequenze di Controllo Osservate**
```
Sequenza ACCESO:
  REG 101 = 0x4003 (FREDDO + ACCESO + FAN MAX)
  REG 102 = 0xA0 (16.0°C)
  REG 103 = 0xAF (Modo stagionale)

Sequenza SPENTO:
  REG 101 = 0x4083 (FREDDO + STANDBY + FAN MAX)
  REG 102 = 0xA0 (16.0°C - stesso valore)
  REG 103 = 0xAF (Modo stagionale - stesso valore)

Differenza: Solo BIT 7 di REG 101 cambia (0=acceso, 1=spento)
```

### 7. **Temperatura = Celsius × 10**
| Celsius | Decimal | Hex | Esempio |
|---------|---------|-----|---------|
| 5.0°C | 50 | 0x32 | Min |
| 16.0°C | 160 | 0xA0 | Default |
| 20.0°C | 200 | 0xC8 | |
| 23.0°C | 230 | 0xE6 | Master originale |
| 35.0°C | 350 | 0x15E | Max (firmware) |

---

## ❌ Problemi Riscontrati e Soluzioni

### Problema 1: "Accende solo 1 ventilo, devo ripremere 2-3 volte"
**Causa:** Baud rate 9600 = TOO SLOW
**Soluzione:** Cambiare a 19200 baud
**Risultato:** ✅ Accendono tutti e 3 con UN click

### Problema 2: "Non accende nulla con indirizzi 1, 2, 3"
**Causa:** Master usa broadcast (0x00), non indirizzi separati
**Soluzione:** Tornare a indirizzo 0x00 (broadcast)
**Risultato:** ✅ Tutti ricevono lo STESSO comando

### Problema 3: "Lo sniffer non cattura nulla"
**Causa 1:** SNIFFER_MODE disabilitato → Commentare `// #define SNIFFER_MODE`
**Causa 2:** Baud rate sbagliato
**Soluzione:** Abilitare SNIFFER_MODE + usare baud rate corretto (9600 per sniff)
**Risultato:** ✅ Cattura i frame dal Master

### Problema 4: "REG 103 ha valori diversi (0x008A vs 0xAF vs 0xB9)"
**Causa:** Documentazione incompleta
**Soluzione:** Sniffare il Master originale per trovare il valore vero
**Risultato:** ✅ Valore corretto: 0xAF

### Problema 5: "Non vedo l'indirizzo nei frame dello sniffer"
**Causa:** Tabella HTML non mostrava Addr, Func, Frame Completo
**Soluzione:** Aggiungere colonne all'HTML dello sniffer
**Risultato:** ✅ Ora visibili Addr (giallo), Func (magenta), Frame Completo (verde)

---

## 📋 Configurazione Finale Corretta

```cpp
// BAUD RATE
#define BAUD_RATE 19200  // ✓ Velocità corretta

// INDIRIZZO MODBUS
modbusWriteRegister(0, 101, regConfig);  // 0 = BROADCAST a TUTTI
modbusWriteRegister(0, 102, regTemp);
modbusWriteRegister(0, 103, regMode);

// REGISTRI
uint16_t regConfig = 0x4083;  // FREDDO SPENTO FAN MAX (default)
uint16_t regTemp   = 0x00A0;  // 16.0°C (16 × 10 = 160 = 0xA0)
uint16_t regMode   = 0x00AF;  // ✓ Modo stagionale (confermato dal Master)
```

---

## 🔧 Come Testare

### Test 1: Accendi/Spegni tutti e 3
```bash
# ACCENDI
curl -X POST "http://192.168.0.90/api/power?value=on"

# SPEGNI
curl -X POST "http://192.168.0.90/api/power?value=off"
```
**Aspettato:** Un click accende/spegne tutti e 3 i ventilconvettori contemporaneamente

### Test 2: Sniff dal Master
1. Abilita SNIFFER_MODE nel codice
2. Flashia con BAUD_RATE = 9600 (per sniffing)
3. Apri http://192.168.0.90/sniffer
4. Accendi il termostato originale
5. Guarda i frame catturati
6. Verifica: Addr = 0x00, Reg = 101/102/103, Decodifica corretta

### Test 3: Sniff dal nostro ESP
1. Disabilita SNIFFER_MODE
2. Flashia con BAUD_RATE = 19200 (per controllo)
3. Apri http://192.168.0.90 (pagina controllo)
4. Clicca ACCENDI
5. Apri http://192.168.0.90/sniffer in un'altra scheda
6. Verifica: Frame con Addr=0x00 catturati in tempo reale

---

## 📊 Tabella Hex Rapida

| Azione | REG 101 | REG 102 | REG 103 | Note |
|--------|---------|---------|---------|------|
| ACCESO FREDDO MAX | 0x4003 | 0xA0 (16°C) | 0xAF | Default |
| SPENTO FREDDO MAX | 0x4083 | 0xA0 (16°C) | 0xAF | Bit 7 = 1 |
| ACCESO CALDO MAX | 0x2003 | 0xA0 (16°C) | 0xAF | Bit 13 = 1 |
| SPENTO CALDO MAX | 0x2083 | 0xA0 (16°C) | 0xAF | Bit 13 + 7 |

**Bit 7 = STANDBY (power):** 0=acceso, 1=spento
**Bit 14 = FREDDO:** 1=freddo
**Bit 13 = CALDO:** 1=caldo
**Bit 0-1 = FAN:** 00=auto, 01=min, 10=night, 11=max

---

**Data scoperte:** Aprile 2026
**Stato:** ✅ Funzionante
**Note:** Vedere sniffer per i dettagli dei frame in tempo reale
