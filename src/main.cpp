/*
 * VISLA Modbus Controller per LilyGo T-CAN485
 * Viessmann Energycal Slim W — SOSTITUZIONE COMANDO TOUCH
 *
 * Replica il comportamento del comando touch:
 * Invia Write Single Register (0x06) in broadcast (addr 0)
 * su registri 101, 102, 103 ogni ~60 secondi.
 *
 * Accetta comandi via Serial (USB) per cambiare parametri:
 *   T22.5  → imposta temperatura 22.5°C
 *   ON     → accendi
 *   OFF    → spegni
 *   FAN0   → ventilatore AUTO
 *   FAN1   → ventilatore MIN
 *   FAN2   → ventilatore NIGHT
 *   FAN3   → ventilatore MAX
 *   HEAT   → modo riscaldamento
 *   COOL   → modo raffrescamento
 *   STATUS → mostra stato attuale
 *   SEND   → forza invio immediato
 */

#include <Arduino.h>

// === T-CAN485 Pin Definitions ===
#define RS485_TX_PIN       22
#define RS485_RX_PIN       21
#define RS485_RE_PIN       17
#define RS485_SHUTDOWN_PIN 19
#define BOOST_ENABLE_PIN   16

#define RS485 Serial1
#define BAUD_RATE 9600
#define SEND_INTERVAL 10000  // Invio ogni 10 secondi (più frequente del touch per reattività)

// === Stato ventilconvettore ===
// Reg 101 (0x4003 di default):
//   bit 0-1: velocità ventilatore (0=AUTO, 1=MIN, 2=NIGHT, 3=MAX)
//   bit 14: acceso (1) / spento (0)
uint16_t regConfig = 0x4003;  // Default: acceso, fan AUTO? (bit0=1,bit1=1 → fan MAX?)

// Reg 102: temperatura setpoint × 10
uint16_t regTemp = 0x00CD;    // Default: 20.5°C (205)

// Reg 103: modo stagionale
uint16_t regMode = 0x0082;    // Default dal touch

// === Utilità Modbus ASCII ===

uint8_t calculateLRC(uint8_t *data, int len) {
  uint8_t lrc = 0;
  for (int i = 0; i < len; i++) lrc += data[i];
  return (uint8_t)(-(int8_t)lrc);
}

void modbusWriteRegister(uint8_t addr, uint16_t reg, uint16_t value) {
  uint8_t payload[6];
  payload[0] = addr;
  payload[1] = 0x06;
  payload[2] = (reg >> 8) & 0xFF;
  payload[3] = reg & 0xFF;
  payload[4] = (value >> 8) & 0xFF;
  payload[5] = value & 0xFF;

  uint8_t lrc = calculateLRC(payload, 6);

  char txBuf[32];
  int pos = 0;
  txBuf[pos++] = ':';
  for (int i = 0; i < 6; i++) pos += sprintf(&txBuf[pos], "%02X", payload[i]);
  pos += sprintf(&txBuf[pos], "%02X", lrc);
  txBuf[pos++] = '\r';
  txBuf[pos++] = '\n';

  while (RS485.available()) RS485.read();
  RS485.write((uint8_t*)txBuf, pos);
  RS485.flush();
}

// Invia tutti i registri al ventilconvettore (come faceva il touch)
void sendAllRegisters() {
  Serial.println(">>> Invio registri al ventilconvettore:");
  Serial.printf("    Reg 101 = 0x%04X (config)\n", regConfig);
  Serial.printf("    Reg 102 = 0x%04X (%.1f°C)\n", regTemp, regTemp / 10.0);
  Serial.printf("    Reg 103 = 0x%04X (modo)\n", regMode);

  modbusWriteRegister(0, 101, regConfig);
  delay(1000);  // Pausa tra frame come il touch (~1.1 sec)

  modbusWriteRegister(0, 102, regTemp);
  delay(1000);

  modbusWriteRegister(0, 103, regMode);

  Serial.println("    OK - inviati!");
}

void printStatus() {
  Serial.println();
  Serial.println("=== STATO ATTUALE ===");

  // Temperatura
  Serial.printf("  Temperatura setpoint: %.1f°C\n", regTemp / 10.0);

  // On/Off
  bool isOn = (regConfig >> 14) & 1;
  Serial.printf("  Stato: %s\n", isOn ? "ACCESO" : "SPENTO");

  // Ventilatore
  uint8_t fanSpeed = regConfig & 0x03;
  const char* fanNames[] = {"AUTO", "MIN", "NIGHT", "MAX"};
  Serial.printf("  Ventilatore: %s (%d)\n", fanNames[fanSpeed], fanSpeed);

  // Modo
  Serial.printf("  Modo (reg 103): 0x%04X\n", regMode);
  // 0x0082 potrebbe essere: byte basso bit 1 = riscaldamento?
  // 0x0082 = 130 = 1000 0010 → bit 1 = 1 (caldo?), bit 7 = 1 (?)
  bool heating = (regMode & 0x02) ? true : false;
  Serial.printf("  Stagione: %s (ipotesi)\n", heating ? "CALDO" : "FREDDO");

  Serial.printf("\n  Registri raw: 101=0x%04X 102=0x%04X 103=0x%04X\n",
                regConfig, regTemp, regMode);
  Serial.println("=====================\n");
}

void printHelp() {
  Serial.println();
  Serial.println("=== COMANDI DISPONIBILI ===");
  Serial.println("  T22.5   → imposta temperatura 22.5°C");
  Serial.println("  ON      → accendi ventilconvettore");
  Serial.println("  OFF     → spegni ventilconvettore");
  Serial.println("  FAN0    → ventilatore AUTO");
  Serial.println("  FAN1    → ventilatore MIN");
  Serial.println("  FAN2    → ventilatore NIGHT");
  Serial.println("  FAN3    → ventilatore MAX");
  Serial.println("  HEAT    → modo riscaldamento");
  Serial.println("  COOL    → modo raffrescamento");
  Serial.println("  STATUS  → mostra stato");
  Serial.println("  SEND    → forza invio immediato");
  Serial.println("  HELP    → mostra questo menu");
  Serial.println("===========================\n");
}

// Processa comando ricevuto da Serial USB
void processCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();

  if (cmd.length() == 0) return;

  bool changed = false;

  if (cmd.startsWith("T")) {
    // Imposta temperatura: T22.5
    float temp = cmd.substring(1).toFloat();
    if (temp >= 5.0 && temp <= 35.0) {
      regTemp = (uint16_t)(temp * 10);
      Serial.printf(">>> Temperatura impostata: %.1f°C (reg 102 = 0x%04X)\n", temp, regTemp);
      changed = true;
    } else {
      Serial.println("!!! Temperatura fuori range (5-35°C)");
    }
  }
  else if (cmd == "ON") {
    regConfig |= (1 << 14);  // Set bit 14
    Serial.println(">>> Ventilconvettore ACCESO");
    changed = true;
  }
  else if (cmd == "OFF") {
    regConfig &= ~(1 << 14);  // Clear bit 14
    Serial.println(">>> Ventilconvettore SPENTO");
    changed = true;
  }
  else if (cmd.startsWith("FAN") && cmd.length() == 4) {
    int speed = cmd.charAt(3) - '0';
    if (speed >= 0 && speed <= 3) {
      regConfig = (regConfig & ~0x03) | (speed & 0x03);
      const char* names[] = {"AUTO", "MIN", "NIGHT", "MAX"};
      Serial.printf(">>> Ventilatore: %s\n", names[speed]);
      changed = true;
    }
  }
  else if (cmd == "HEAT") {
    regMode |= 0x02;   // Set bit 1
    Serial.println(">>> Modo: RISCALDAMENTO");
    changed = true;
  }
  else if (cmd == "COOL") {
    regMode &= ~0x02;  // Clear bit 1
    Serial.println(">>> Modo: RAFFRESCAMENTO");
    changed = true;
  }
  else if (cmd == "STATUS") {
    printStatus();
  }
  else if (cmd == "SEND") {
    sendAllRegisters();
  }
  else if (cmd == "HELP") {
    printHelp();
  }
  else {
    Serial.printf("??? Comando sconosciuto: %s\n", cmd.c_str());
    Serial.println("    Scrivi HELP per lista comandi");
  }

  if (changed) {
    Serial.println("    Invio immediato...");
    sendAllRegisters();
    printStatus();
  }
}

// === Ascolto bus (per debug) ===
#define MAX_FRAME_LEN 256
char frameBuf[MAX_FRAME_LEN];
int fPos = 0;
bool fActive = false;

void processRxByte(char c) {
  if (c == ':') {
    fPos = 0;
    fActive = true;
  } else if (c == '\r') {
    // ignora
  } else if (c == '\n' && fActive) {
    frameBuf[fPos] = '\0';
    if (fPos > 0) {
      Serial.printf("[BUS RX] :%s\n", frameBuf);
    }
    fActive = false;
    fPos = 0;
  } else if (fActive && fPos < MAX_FRAME_LEN - 1) {
    frameBuf[fPos++] = c;
  }
}

void initRS485() {
  pinMode(BOOST_ENABLE_PIN, OUTPUT);
  digitalWrite(BOOST_ENABLE_PIN, HIGH);
  delay(100);
  pinMode(RS485_SHUTDOWN_PIN, OUTPUT);
  digitalWrite(RS485_SHUTDOWN_PIN, HIGH);
  pinMode(RS485_RE_PIN, OUTPUT);
  digitalWrite(RS485_RE_PIN, HIGH);
  RS485.begin(BAUD_RATE, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  delay(200);
}

// === MAIN ===

unsigned long lastSend = 0;
String serialInput = "";

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("=============================================");
  Serial.println("  VISLA Controller - Viessmann Energycal Slim W");
  Serial.println("  T-CAN485 come sostituto del comando touch");
  Serial.println("=============================================\n");

  initRS485();

  printHelp();
  printStatus();

  // Primo invio immediato
  Serial.println(">>> Primo invio registri...\n");
  sendAllRegisters();

  lastSend = millis();
}

void loop() {
  // Leggi comandi da Serial USB
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialInput.length() > 0) {
        processCommand(serialInput);
        serialInput = "";
      }
    } else {
      serialInput += c;
    }
  }

  // Ascolta bus RS485 (debug)
  while (RS485.available()) {
    uint8_t raw = RS485.read();
    char c = (char)(raw & 0x7F);
    processRxByte(c);
  }

  // Invio periodico
  if (millis() - lastSend >= SEND_INTERVAL) {
    lastSend = millis();
    sendAllRegisters();
  }
}
