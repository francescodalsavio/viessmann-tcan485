/*
 * VISLA Modbus Controller per LilyGo T-CAN485
 * Viessmann Energycal Slim W — SOSTITUZIONE COMANDO TOUCH
 *
 * Controllo via:
 *   1. Seriale USB (comandi testuali)
 *   2. API REST via WiFi (http://<ip>/api/...)
 *
 * API REST endpoints:
 *   GET  /api/status              → stato JSON
 *   POST /api/temperature?value=22.5
 *   POST /api/power?value=on|off
 *   POST /api/fan?value=0|1|2|3   (auto/min/night/max)
 *   POST /api/mode?value=heat|cool
 *
 * MODALITÀ SNIFFER:
 *   Decommentare #define SNIFFER_MODE per attivare modalità ascolto passivo
 *   Non invierà comandi, solo ascolterà il master originale
 */

#define SNIFFER_MODE  // Modalità sniffer + controllo attivo

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

// === WiFi Config ===
// CAMBIA QUESTI CON I TUOI DATI!
const char* WIFI_SSID = "Molinella";
const char* WIFI_PASS = "Fastweb10";

// === T-CAN485 Pin Definitions ===
#define RS485_TX_PIN       22
#define RS485_RX_PIN       21
#define RS485_RE_PIN       17
#define RS485_SHUTDOWN_PIN 19
#define BOOST_ENABLE_PIN   16

#define RS485 Serial1
#define BAUD_RATE 9600
#define SEND_INTERVAL 65000  // 65 sec - replicates Master Viessmann timing

// === Stato ventilconvettore ===
// Reg 101: bit 14=FREDDO(blu), bit 13=CALDO(rosso), bit 7=STANDBY, bit 0-1=fan speed
// ACCESO CALDO:  bit 13 set, bit 7 clear (es. 0x2003)
// ACCESO FREDDO: bit 14 set, bit 7 clear (es. 0x4003)
// SPENTO:        bit 7 set (es. 0x2083 = caldo+standby, 0x4083 = freddo+standby)
uint16_t regConfig = 0x2003;  // caldo acceso (bit13), ventola MAX
uint16_t regTemp   = 0x00CD;  // 20.5°C (× 10)
uint16_t regMode   = 0x008A;  // modo stagionale
bool     powerOn   = true;
bool     heating   = true;    // true=caldo, false=freddo

// === Web Server ===
WebServer server(80);

// === Sniffer Mode - Frame Capture Buffer ===
#define SNIFFER_BUFFER_SIZE 100
struct SniffedFrame {
  uint32_t timestamp;
  uint8_t addr;
  uint8_t func;
  uint16_t reg;
  uint16_t val;
  uint8_t lrc;
  bool lrc_ok;
  char dateTime[16];  // "HH:MM:SS" salvato al momento della ricezione
};
SniffedFrame sniffBuffer[SNIFFER_BUFFER_SIZE];
int sniffIndex = 0;

// === Utilità Modbus ASCII ===

uint8_t calculateLRC(uint8_t *data, int len) {
  uint8_t lrc = 0;
  for (int i = 0; i < len; i++) lrc += data[i];
  return (uint8_t)(-(int8_t)lrc);
}

bool parseModbusFrame(const char* hexStr, SniffedFrame* frame) {
  // Parsing frame ASCII: AAFFRRRRRRRRLLCC (6 bytes payload + lrc = 7 bytes total)
  // Frame format: ADDR(2) FUNC(2) REG_HI(2) REG_LO(2) VAL_HI(2) VAL_LO(2) LRC(2) = 14 chars
  int len = strlen(hexStr);
  if (len < 14) return false;

  uint8_t data[7];
  for (int i = 0; i < 7 && i*2 < len; i++) {
    char hex[3] = {hexStr[i*2], hexStr[i*2+1], 0};
    data[i] = (uint8_t)strtol(hex, NULL, 16);
  }

  frame->timestamp = millis();
  frame->addr = data[0];
  frame->func = data[1];
  frame->reg = (data[2] << 8) | data[3];
  frame->val = (data[4] << 8) | data[5];
  frame->lrc = data[6];

  // Calcola Data/Ora immutabile al momento della ricezione (mm:ss:ms)
  uint32_t totalMs = frame->timestamp;
  uint32_t totalSec = totalMs / 1000;
  uint32_t ms = totalMs % 1000;
  uint32_t mm = totalSec / 60;
  uint32_t ss = totalSec % 60;
  snprintf(frame->dateTime, sizeof(frame->dateTime), "%02lu:%02lu.%03lu", mm, ss, ms);

  // Verify LRC
  uint8_t lrc_calc = calculateLRC(data, 6);
  frame->lrc_ok = (frame->lrc == lrc_calc);

  return true;
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

void sendAllRegisters() {
#ifdef SNIFFER_MODE
  Serial.println(">>> [SNIFFER MODE] Invio disabilitato, ascolto passivo...");
  return;
#endif
  Serial.println(">>> Invio registri...");
  modbusWriteRegister(0, 101, regConfig);
  delay(1160);  // 1.16 sec - matches Master Viessmann timing
  yield();
  modbusWriteRegister(0, 102, regTemp);
  delay(1160);  // 1.16 sec - matches Master Viessmann timing
  yield();
  modbusWriteRegister(0, 103, regMode);
  Serial.printf("    101=0x%04X 102=0x%04X(%.1f&deg;C) 103=0x%04X OK\n",
                regConfig, regTemp, regTemp / 10.0, regMode);
}

// === Helper per stato ===

bool isOn()      { return powerOn; }
int  fanSpeed()  { return regConfig & 0x03; }
bool isHeating() { return heating; }
float getTemp()  { return regTemp / 10.0; }

const char* fanName() {
  const char* names[] = {"auto", "min", "night", "max"};
  return names[fanSpeed()];
}

String statusJSON() {
  String json = "{";
  json += "\"power\":\"" + String(isOn() ? "on" : "off") + "\",";
  json += "\"temperature\":" + String(getTemp(), 1) + ",";
  json += "\"fan\":\"" + String(fanName()) + "\",";
  json += "\"fan_speed\":" + String(fanSpeed()) + ",";
  json += "\"mode\":\"" + String(isHeating() ? "heat" : "cool") + "\",";
  json += "\"reg101\":\"0x" + String(regConfig, HEX) + "\",";
  json += "\"reg102\":\"0x" + String(regTemp, HEX) + "\",";
  json += "\"reg103\":\"0x" + String(regMode, HEX) + "\"";
  json += "}";
  return json;
}

// === API REST Handlers ===

void handleStatus() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", statusJSON());
}

void handleTemperature() {
  if (!server.hasArg("value")) {
    server.send(400, "application/json", "{\"error\":\"manca parametro value\"}");
    return;
  }
  float temp = server.arg("value").toFloat();
  if (temp < 5.0 || temp > 35.0) {
    server.send(400, "application/json", "{\"error\":\"temperatura fuori range 5-35\"}");
    return;
  }
  regTemp = (uint16_t)(temp * 10);
  sendAllRegisters();
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", statusJSON());
  Serial.printf("API: Temperatura -> %.1f&deg;C\n", temp);
}

void handlePower() {
  if (!server.hasArg("value")) {
    server.send(400, "application/json", "{\"error\":\"manca parametro value (on/off)\"}");
    return;
  }
  String val = server.arg("value");
  val.toLowerCase();
  if (val == "on") {
    regConfig &= ~((1 << 14) | (1 << 13) | (1 << 7));  // clear mode + standby
    if (heating) regConfig |= (1 << 13);   // caldo = bit 13 (rosso)
    else         regConfig |= (1 << 14);   // freddo = bit 14 (blu)
    powerOn = true;
    sendAllRegisters();
  } else if (val == "off") {
    regConfig |= (1 << 7);   // bit 7 = standby → spegnimento istantaneo
    powerOn = false;
    sendAllRegisters();       // manda il comando di spegnimento
    Serial.println(">>> SPENTO (bit 7 standby)");
  } else {
    server.send(400, "application/json", "{\"error\":\"value deve essere on o off\"}");
    return;
  }
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", statusJSON());
  Serial.printf("API: Power -> %s\n", val.c_str());
}

void handleFan() {
  if (!server.hasArg("value")) {
    server.send(400, "application/json", "{\"error\":\"manca parametro value (0-3)\"}");
    return;
  }
  String val = server.arg("value");
  int speed = -1;
  if (val == "auto" || val == "0") speed = 0;
  else if (val == "min" || val == "1") speed = 1;
  else if (val == "night" || val == "2") speed = 2;
  else if (val == "max" || val == "3") speed = 3;

  if (speed < 0) {
    server.send(400, "application/json", "{\"error\":\"value: 0/auto, 1/min, 2/night, 3/max\"}");
    return;
  }
  regConfig = (regConfig & ~0x03) | (speed & 0x03);
  sendAllRegisters();
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", statusJSON());
  Serial.printf("API: Fan -> %s\n", fanName());
}

void handleMode() {
  if (!server.hasArg("value")) {
    server.send(400, "application/json", "{\"error\":\"manca parametro value (heat/cool)\"}");
    return;
  }
  String val = server.arg("value");
  val.toLowerCase();
  if (val == "heat") {
    heating = true;
    regMode |= 0x02;
    if (powerOn) { regConfig &= ~(1 << 14); regConfig |= (1 << 13); }  // caldo = bit 13
  } else if (val == "cool") {
    heating = false;
    regMode &= ~0x02;
    if (powerOn) { regConfig &= ~(1 << 13); regConfig |= (1 << 14); }  // freddo = bit 14
  } else {
    server.send(400, "application/json", "{\"error\":\"value deve essere heat o cool\"}");
    return;
  }
  sendAllRegisters();
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", statusJSON());
  Serial.printf("API: Mode -> %s\n", val.c_str());
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>VISLA - Ventilconvettore</title>";
  html += "<style>";
  html += "body{font-family:sans-serif;max-width:400px;margin:20px auto;padding:0 15px;background:#1a1a2e;color:#eee}";
  html += "h1{color:#e94560;font-size:1.4em;text-align:center}";
  html += ".card{background:#16213e;border-radius:12px;padding:15px;margin:10px 0}";
  html += ".row{display:flex;justify-content:space-between;align-items:center;padding:8px 0}";
  html += ".label{color:#aaa;font-size:0.9em}";
  html += ".value{font-size:1.2em;font-weight:bold}";
  html += "button{background:#e94560;color:white;border:none;border-radius:8px;padding:12px 20px;";
  html += "font-size:1em;cursor:pointer;margin:4px;flex:1}";
  html += "button:active{background:#c73e54}";
  html += "button.off{background:#444}";
  html += "button.cool{background:#0f3460}";
  html += ".temp-ctrl{display:flex;align-items:center;justify-content:center;gap:15px;padding:10px}";
  html += ".temp-val{font-size:2.5em;font-weight:bold;color:#e94560;min-width:100px;text-align:center}";
  html += ".temp-btn{width:50px;height:50px;border-radius:50%;font-size:1.5em;flex:none}";
  html += ".btn-row{display:flex;gap:5px}";
  html += "#status{text-align:center;color:#aaa;font-size:0.8em;padding:5px}";
  html += "</style></head><body>";
  html += "<h1>VISLA Ventilconvettore</h1>";

  // Temperatura
  html += "<div class='card'>";
  html += "<div class='row'><span class='label'>Temperatura setpoint</span></div>";
  html += "<div class='temp-ctrl'>";
  html += "<button class='temp-btn' onclick='setTemp(-0.5)'>-</button>";
  html += "<span id='temp' class='temp-val'>--</span>";
  html += "<button class='temp-btn' onclick='setTemp(+0.5)'>+</button>";
  html += "</div></div>";

  // Power
  html += "<div class='card'>";
  html += "<div class='row'><span class='label'>Alimentazione</span></div>";
  html += "<div class='btn-row'>";
  html += "<button id='btn-on' onclick='setPower(\"on\")'>ACCESO</button>";
  html += "<button id='btn-off' class='off' onclick='setPower(\"off\")'>SPENTO</button>";
  html += "</div></div>";

  // Fan
  html += "<div class='card'>";
  html += "<div class='row'><span class='label'>Ventola</span></div>";
  html += "<div class='btn-row'>";
  html += "<button id='fan-0' onclick='setFan(0)'>AUTO</button>";
  html += "<button id='fan-1' onclick='setFan(1)'>MIN</button>";
  html += "<button id='fan-2' onclick='setFan(2)'>NIGHT</button>";
  html += "<button id='fan-3' onclick='setFan(3)'>MAX</button>";
  html += "</div></div>";

  // Mode
  html += "<div class='card'>";
  html += "<div class='row'><span class='label'>Stagione</span></div>";
  html += "<div class='btn-row'>";
  html += "<button id='btn-heat' onclick='setMode(\"heat\")'>CALDO</button>";
  html += "<button id='btn-cool' class='cool' onclick='setMode(\"cool\")'>FREDDO</button>";
  html += "</div></div>";

  html += "<div id='status'>Caricamento...</div>";

  // JavaScript
  html += "<script>";
  html += "var currentTemp=20.5;";
  html += "function api(path,cb){fetch(path,{method:'POST'}).then(r=>r.json()).then(d=>{update(d);if(cb)cb(d)}).catch(e=>document.getElementById('status').innerText='Errore: '+e)}";
  html += "function update(d){";
  html += "currentTemp=d.temperature;";
  html += "document.getElementById('temp').innerText=d.temperature.toFixed(1)+'\\u00B0C';";
  html += "document.getElementById('btn-on').style.background=d.power=='on'?'#e94560':'#444';";
  html += "document.getElementById('btn-off').style.background=d.power=='off'?'#e94560':'#444';";
  html += "document.getElementById('btn-heat').style.background=d.mode=='heat'?'#e94560':'#444';";
  html += "document.getElementById('btn-cool').style.background=d.mode=='cool'?'#0f3460':'#444';";
  html += "for(var i=0;i<4;i++)document.getElementById('fan-'+i).style.background=d.fan_speed==i?'#e94560':'#444';";
  html += "document.getElementById('status').innerText='Ultimo aggiornamento: '+new Date().toLocaleTimeString()}";
  html += "function setTemp(delta){currentTemp+=delta;if(currentTemp<5)currentTemp=5;if(currentTemp>35)currentTemp=35;api('/api/temperature?value='+currentTemp.toFixed(1))}";
  html += "function setPower(v){api('/api/power?value='+v)}";
  html += "function setFan(v){api('/api/fan?value='+v)}";
  html += "function setMode(v){api('/api/mode?value='+v)}";
  html += "fetch('/api/status').then(r=>r.json()).then(d=>update(d));";
  html += "setInterval(function(){fetch('/api/status').then(r=>r.json()).then(d=>update(d))},5000);";
  html += "</script></body></html>";

  server.send(200, "text/html", html);
}

void handleReg() {
  if (!server.hasArg("reg") || !server.hasArg("val")) {
    server.send(400, "text/plain", "manca reg o val");
    return;
  }
  int reg = server.arg("reg").toInt();
  uint16_t val = (uint16_t)strtol(server.arg("val").c_str(), NULL, 0);
  if (reg == 101) regConfig = val;
  else if (reg == 103) regMode = val;
  modbusWriteRegister(0, reg, val);
  delay(200);
  yield();
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", statusJSON());
  Serial.printf("API: reg %d = 0x%04X\n", reg, val);
}

void handleTest() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>VISLA TEST</title>";
  html += "<style>";
  html += "body{font-family:monospace;max-width:500px;margin:10px auto;padding:0 10px;background:#111;color:#eee}";
  html += "h2{color:#e94560;margin:15px 0 5px}";
  html += "button{background:#333;color:#fff;border:1px solid #555;border-radius:6px;padding:10px 12px;";
  html += "font-size:0.9em;cursor:pointer;margin:3px;font-family:monospace}";
  html += "button:active{background:#e94560}";
  html += "button.on{background:#27ae60}";
  html += "#log{background:#000;padding:10px;border-radius:6px;font-size:0.8em;max-height:200px;overflow-y:auto;margin:10px 0}";
  html += "</style></head><body>";
  html += "<h1 style='color:#e94560'>VISLA - Test Registri</h1>";
  html += "<div id='log'>Premi un bottone...</div>";

  // Reg 101 tests
  html += "<h2>REG 101 (config)</h2>";
  html += "<button onclick='r(101,0x4003)'>0x4003 CALDO ON</button>";
  html += "<button onclick='r(101,0x4001)'>0x4001 CALDO MIN</button>";
  html += "<button onclick='r(101,0x4081)'>0x4081 CALDO+b7</button>";
  html += "<button onclick='r(101,0x2003)'>0x2003 FREDDO?</button>";
  html += "<button onclick='r(101,0x2083)'>0x2083 FREDDO+b7</button>";
  html += "<button onclick='r(101,0x0003)'>0x0003 AUTO</button>";
  html += "<button onclick='r(101,0x0000)'>0x0000 ZERO</button>";
  html += "<button onclick='r(101,0x6003)'>0x6003 b14+b13</button>";
  html += "<button onclick='r(101,0x8003)'>0x8003 b15</button>";
  html += "<button onclick='r(101,0xC003)'>0xC003 b15+b14</button>";
  html += "<button onclick='r(101,0xFFFF)'>0xFFFF TUTTI</button>";

  // Reg 103 tests
  html += "<h2>REG 103 (modo)</h2>";
  html += "<button onclick='r(103,0x008A)'>0x008A (touch)</button>";
  html += "<button onclick='r(103,0x0082)'>0x0082 caldo old</button>";
  html += "<button onclick='r(103,0x0080)'>0x0080 freddo old</button>";
  html += "<button onclick='r(103,0x0088)'>0x0088</button>";
  html += "<button onclick='r(103,0x7FFF)'>0x7FFF</button>";
  html += "<button onclick='r(103,0xFFFF)'>0xFFFF TUTTI</button>";
  html += "<button onclick='r(103,0x0000)'>0x0000 ZERO</button>";

  // Reg 102 tests
  html += "<h2>REG 102 (temp)</h2>";
  html += "<button onclick='r(102,0x00CD)'>20.5\\u00B0C</button>";
  html += "<button onclick='r(102,0x0032)'>5.0\\u00B0C</button>";
  html += "<button onclick='r(102,0x0000)'>0 (zero)</button>";
  html += "<button onclick='r(102,0xFFFF)'>0xFFFF</button>";

  // Special combos
  html += "<h2>COMBO</h2>";
  html += "<button style='background:#27ae60' onclick='combo([101,0x4003],[103,0x008A])'>ACCENDI CALDO</button>";
  html += "<button style='background:#0f3460' onclick='combo([101,0x2003],[103,0x008A])'>ACCENDI FREDDO</button>";
  html += "<button style='background:#e94560' onclick='spegni()'>SPEGNI (bit7)</button>";
  html += "<button style='background:#e94560' onclick='stopSend()'>STOP INVIO (silenzio)</button>";
  html += "<button style='background:#9933ff' onclick='autoTest()'>▶ AUTO TEST (26 cmd)</button>";
  html += "<button style='background:#ff6600' onclick='stopAutoTest()'>⏸ FERMA</button>";
  html += "<span id='autoStatus' style='color:#aaa;margin-left:10px;font-size:0.9em'></span>";

  // Custom register input
  html += "<h2>REGISTRO CUSTOM</h2>";
  html += "<div style='display:flex;gap:5px;align-items:center;flex-wrap:wrap'>";
  html += "<select id='creg'><option value='101'>101</option><option value='102'>102</option><option value='103'>103</option></select>";
  html += "<input id='cval' type='text' placeholder='0x4083' style='background:#222;color:#fff;border:1px solid #555;border-radius:6px;padding:8px;font-family:monospace;width:100px'>";
  html += "<button onclick='sendCustom()'>INVIA</button>";
  html += "</div>";

  // Documenta azioni
  html += "<h2>DOCUMENTA AZIONE</h2>";
  html += "<div style='display:flex;gap:5px;flex-wrap:wrap'>";
  html += "<input id='noteInput' type='text' placeholder='es. Accende freddo a 17°C' style='background:#222;color:#fff;border:1px solid #555;border-radius:6px;padding:8px;flex:1;font-family:monospace'>";
  html += "<button onclick='addNote()' style='background:#555'>NOTA</button>";
  html += "</div>";
  html += "<div id='notesList' style='background:#000;padding:10px;border-radius:6px;font-size:0.85em;max-height:150px;overflow-y:auto;margin-top:10px;color:#0f0'></div>";

  // Copy log
  html += "<h2>LOG</h2>";
  html += "<button style='background:#555' onclick='copyLog()'>COPIA LOG</button>";
  html += "<button style='background:#555;margin-left:5px' onclick='downloadNotes()'>SCARICA NOTE</button>";

  // JavaScript
  html += "<script>";
  html += "var logEl=document.getElementById('log');";
  html += "var notes=JSON.parse(localStorage.getItem('viessmannNotes')||'[]');";
  html += "var autoRunning=false;";
  html += "var autoIndex=0;";
  html += "var autoTests=[[101,0x4003],[101,0x4001],[101,0x4081],[101,0x2003],[101,0x2083],[101,0x0003],[101,0x0000],[101,0x6003],[101,0x8003],[101,0xC003],[101,0xFFFF],[103,0x008A],[103,0x0082],[103,0x0080],[103,0x0088],[103,0x7FFF],[103,0xFFFF],[103,0x0000],[102,0x00CD],[102,0x0032],[102,0x0000],[102,0xFFFF]];";
  html += "function log(t){logEl.innerHTML=t+'<br>'+logEl.innerHTML}";
  html += "function updateAutoStatus(){var el=document.getElementById('autoStatus');if(autoRunning){el.innerText='Test '+autoIndex+'/'+autoTests.length}else{el.innerText=''}}";
  html += "function r(reg,val){";
  html += "  log('Invio reg '+reg+' = 0x'+val.toString(16).toUpperCase()+'...');";
  html += "  fetch('/api/reg?reg='+reg+'&val='+val).then(r=>r.json()).then(d=>{";
  html += "    log('OK: 101='+d.reg101+' 102='+d.reg102+' 103='+d.reg103);";
  html += "  }).catch(e=>log('ERRORE: '+e))}";
  html += "function autoTest(){if(autoRunning)return;autoRunning=true;autoIndex=0;autoLoop()}";
  html += "function autoLoop(){if(!autoRunning||autoIndex>=autoTests.length){autoRunning=false;updateAutoStatus();log('AUTO TEST COMPLETO');return}";
  html += "var t=autoTests[autoIndex];autoIndex++;updateAutoStatus();r(t[0],t[1]);setTimeout(autoLoop,2000)}";
  html += "function stopAutoTest(){autoRunning=false;updateAutoStatus();log('AUTO TEST FERMATO')}";
  html += "function combo(){for(var i=0;i<arguments.length;i++){var a=arguments[i];r(a[0],a[1])}}";
  html += "function spegni(){r(101,(regConfig|0x0080)>>>0)}";
  html += "var regConfig=0x4003;";
  html += "function stopSend(){fetch('/api/power?value=off').then(r=>r.json()).then(d=>log('STOP invio')).catch(e=>log('ERR:'+e))}";
  html += "function sendCustom(){var reg=parseInt(document.getElementById('creg').value);var v=parseInt(document.getElementById('cval').value);if(isNaN(v)){log('Valore non valido');return}r(reg,v)}";
  html += "function addNote(){var text=document.getElementById('noteInput').value.trim();if(!text)return;notes.push({time:new Date().toLocaleString(),text:text});localStorage.setItem('viessmannNotes',JSON.stringify(notes));document.getElementById('noteInput').value='';updateNotesList()}";
  html += "function updateNotesList(){var el=document.getElementById('notesList');el.innerHTML=notes.map((n,i)=>'<div style=\"border-bottom:1px solid #333;padding:5px 0\">'+n.time+' - '+n.text+'</div>').join('')}";
  html += "function downloadNotes(){var csv='Data,Ora,Nota\\n';notes.forEach(n=>{csv+=n.time+','+n.text+'\\n'});var blob=new Blob([csv],{type:'text/csv'});var url=URL.createObjectURL(blob);var a=document.createElement('a');a.href=url;a.download='viessmann_note.csv';document.body.appendChild(a);a.click();document.body.removeChild(a);URL.revokeObjectURL(url)}";
  html += "updateNotesList();";
  html += "function copyLog(){navigator.clipboard.writeText(logEl.innerText).then(()=>alert('Log copiato!')).catch(()=>{var t=document.createElement('textarea');t.value=logEl.innerText;document.body.appendChild(t);t.select();document.execCommand('copy');document.body.removeChild(t);alert('Log copiato!')})}";
  html += "</script></body></html>";

  server.send(200, "text/html", html);
}

void handleResetSniffer() {
#ifdef SNIFFER_MODE
  sniffIndex = 0;
  memset(sniffBuffer, 0, sizeof(sniffBuffer));
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", "{\"status\":\"reset\",\"message\":\"Buffer svuotato\"}");
  Serial.println(">>> Sniffer buffer reset!");
#else
  server.send(403, "text/plain", "SNIFFER MODE non abilitato");
#endif
}

void handleSniffer() {
#ifndef SNIFFER_MODE
  server.send(403, "text/plain", "SNIFFER MODE non abilitato. Decommenta #define SNIFFER_MODE nel codice.");
  return;
#endif

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<meta http-equiv='refresh' content='1'>";
  html += "<title>VISLA SNIFFER</title>";
  html += "<style>";
  html += "body{font-family:monospace;max-width:900px;margin:10px auto;padding:0 10px;background:#0a0a0a;color:#0f0}";
  html += "h1{color:#0f0;text-align:center;border-bottom:2px solid #0f0;padding:10px}";
  html += "table{width:100%;border-collapse:collapse;font-size:0.9em}";
  html += "th{background:#1a1a1a;color:#0f0;padding:8px;text-align:left;border:1px solid #333}";
  html += "td{padding:6px;border:1px solid #222}";
  html += "tr:nth-child(even){background:#0a0a0a}";
  html += "tr:nth-child(odd){background:#151515}";
  html += ".ok{color:#0f0}.err{color:#f00}";
  html += ".reg101{color:#ff6b6b}.reg102{color:#51cf66}.reg103{color:#4dabf7}";
  html += ".info{background:#1a1a1a;padding:10px;border-radius:4px;margin:10px 0;color:#aaa}";
  html += "</style></head><body>";

  html += "<h1>VISLA SNIFFER - Ascolto Passivo Master</h1>";
  html += "<div class='info'>Frame catturati: <strong>" + String(sniffIndex) + "</strong> | Buffer: <strong>" + String(min(sniffIndex, SNIFFER_BUFFER_SIZE)) + "</strong> / " + String(SNIFFER_BUFFER_SIZE) + "</div>";
  html += "<div style='margin:10px 0;text-align:center'>";
  html += "<button onclick='downloadCSV()' style='background:#27ae60;color:white;padding:10px 20px;border:none;border-radius:6px;cursor:pointer;margin-right:10px'>SCARICA CSV</button>";
  html += "<button onclick='downloadJSON()' style='background:#3498db;color:white;padding:10px 20px;border:none;border-radius:6px;cursor:pointer;margin-right:10px'>SCARICA JSON</button>";
  html += "<button onclick='resetData()' style='background:#e74c3c;color:white;padding:10px 20px;border:none;border-radius:6px;cursor:pointer'>RESET</button>";
  html += "</div>";

  if (sniffIndex == 0) {
    html += "<div class='info' style='color:#f00'>⚠ Nessun frame catturato. Assicurati che il master sia connesso e trasmetta.</div>";
  } else {
    html += "<table>";
    html += "<tr><th>#</th><th>Data/Ora (mm:ss.ms)</th><th>Delta (s)</th><th>Reg</th><th>Valore</th><th>Decodifica</th><th>Azione</th><th>Copia</th><th>LRC</th></tr>";

    int start = max(0, sniffIndex - SNIFFER_BUFFER_SIZE);
    int count = 0;
    uint32_t prevTimestamp = 0;
    for (int i = start; i < sniffIndex && count < SNIFFER_BUFFER_SIZE; i++, count++) {
      SniffedFrame& f = sniffBuffer[i % SNIFFER_BUFFER_SIZE];

      // Calcola delay dal frame precedente
      uint32_t deltaMs = (prevTimestamp == 0) ? 0 : (f.timestamp - prevTimestamp);
      float deltaSec = deltaMs / 1000.0f;
      prevTimestamp = f.timestamp;
      String regClass = "";
      String decode = "";

      if (f.reg == 101) {
        regClass = "reg101";
        decode = "CFG: ";
        if (f.val & (1 << 14)) decode += "COLD ";
        if (f.val & (1 << 13)) decode += "HOT ";
        if (f.val & (1 << 7)) decode += "STANDBY ";
        int fan = f.val & 0x03;
        const char* fans[] = {"AUTO", "MIN", "NIGHT", "MAX"};
        decode += fans[fan];
      } else if (f.reg == 102) {
        regClass = "reg102";
        decode = String(f.val / 10.0, 1) + "°C";
      } else if (f.reg == 103) {
        regClass = "reg103";
        decode = "MODE";
      }

      String lrcMark = f.lrc_ok ? "<span class='ok'>OK</span>" : "<span class='err'>BAD</span>";

      html += "<tr>";
      html += "<td>" + String(i + 1) + "</td>";
      html += "<td style='font-size:0.85em;font-family:monospace;color:#0f0'><strong>" + String(f.dateTime) + "</strong></td>";
      html += "<td style='font-size:0.85em;font-family:monospace;color:#ff9900'><strong>" + String(deltaSec, 2) + "s</strong></td>";
      html += "<td class='" + regClass + "'><strong>" + String(f.reg) + "</strong></td>";
      html += "<td><strong>0x" + String(f.val, HEX) + "</strong></td>";
      html += "<td style='font-size:0.9em'>" + decode + "</td>";
      html += "<td><select style='padding:4px;font-size:0.85em'>";
      html += "<option value=''>--</option>";
      html += "<option value='Accendi CALDO'>Accendi CALDO</option>";
      html += "<option value='Accendi FREDDO'>Accendi FREDDO</option>";
      html += "<option value='Spegni'>Spegni</option>";
      html += "<option value='Temp +'>Temp +</option>";
      html += "<option value='Temp -'>Temp -</option>";
      html += "<option value='Fan AUTO'>Fan AUTO</option>";
      html += "<option value='Fan MIN'>Fan MIN</option>";
      html += "<option value='Fan MAX'>Fan MAX</option>";
      html += "</select></td>";
      html += "<td><button onclick='copyCmd(" + String(f.reg) + ",0x" + String(f.val, HEX) + ")' style='background:#555;color:#0f0;padding:4px 8px;border:none;border-radius:4px;cursor:pointer;font-size:0.8em'>COPIA</button></td>";
      html += "<td>" + lrcMark + "</td>";
      html += "</tr>";
    }
    html += "</table>";
  }

  html += "<div class='info' style='margin-top:20px'>Monitoraggio in tempo reale | Ultimo aggiornamento: " + String(millis() / 1000) + "s</div>";

  // JavaScript per export
  html += "<script>";
  html += "var pageLoadTime=Date.now();";
  html += "function formatTime(ms){";
  html += "  let totalSec=Math.floor(ms/1000);let h=Math.floor(totalSec/3600);let m=Math.floor((totalSec%3600)/60);let s=totalSec%60;";
  html += "  return (h<10?'0':'')+h+':'+(m<10?'0':'')+m+':'+(s<10?'0':'')+s;";
  html += "}";
  html += "function getDateTime(ms){";
  html += "  let rows=document.querySelectorAll('table tr');if(rows.length<2)return 'xx:xx:xx';";
  html += "  let firstMs=parseInt(rows[1].querySelector('.dt').getAttribute('data-ms'));";
  html += "  let offset=pageLoadTime-firstMs;let frameTime=offset+ms;";
  html += "  let d=new Date(frameTime);";
  html += "  let h=String(d.getHours()).padStart(2,'0');let m=String(d.getMinutes()).padStart(2,'0');let s=String(d.getSeconds()).padStart(2,'0');";
  html += "  return h+':'+m+':'+s;";
  html += "}";
  html += "function updateTimestamps(){";
  html += "  document.querySelectorAll('.ts').forEach(el=>{el.textContent=formatTime(parseInt(el.getAttribute('data-ms')))});";
  html += "  document.querySelectorAll('.dt').forEach(el=>{el.textContent=getDateTime(parseInt(el.getAttribute('data-ms')))});";
  html += "}";
  html += "setInterval(updateTimestamps,1000);updateTimestamps();";
  html += "function downloadCSV(){";
  html += "  let csv='#,Tempo,Registro,Valore,Decodifica,Azione\\n';";
  html += "  let rows=document.querySelectorAll('table tr');";
  html += "  for(let i=1;i<rows.length;i++){";
  html += "    let cells=rows[i].querySelectorAll('td');";
  html += "    let actionSelect=rows[i].querySelector('select');";
  html += "    let action=actionSelect?actionSelect.value:'';";
  html += "    csv+=cells[0].textContent+','+cells[1].textContent+','+cells[2].textContent+','+cells[3].textContent+','+cells[4].textContent+','+action+'\\n';";
  html += "  }";
  html += "  downloadFile(csv,'sniffer_log.csv','text/csv');";
  html += "}";
  html += "function downloadJSON(){";
  html += "  let data=[];";
  html += "  let rows=document.querySelectorAll('table tr');";
  html += "  for(let i=1;i<rows.length;i++){";
  html += "    let cells=rows[i].querySelectorAll('td');";
  html += "    let actionSelect=rows[i].querySelector('select');";
  html += "    let action=actionSelect?actionSelect.value:'';";
  html += "    data.push({num:cells[0].textContent,tempo:cells[1].textContent,registro:cells[2].textContent,valore:cells[3].textContent,decodifica:cells[4].textContent,azione:action});";
  html += "  }";
  html += "  downloadFile(JSON.stringify(data,null,2),'sniffer_log.json','application/json');";
  html += "}";
  html += "function resetData(){if(confirm('Reset tutti i dati? Non si puo annullare')){fetch('/api/reset-sniffer').then(r=>r.json()).then(d=>{alert('Buffer svuotato!');location.reload()}).catch(e=>alert('Errore: '+e))}}";
  html += "function copyCmd(reg,val){let cmd='R'+reg+' '+val;navigator.clipboard.writeText(cmd).then(()=>{alert('Copiato: '+cmd)}).catch(()=>{var t=document.createElement('textarea');t.value=cmd;document.body.appendChild(t);t.select();document.execCommand('copy');document.body.removeChild(t);alert('Copiato: '+cmd)})}";
  html += "function downloadFile(content,filename,type){";
  html += "  let blob=new Blob([content],{type:type});";
  html += "  let url=URL.createObjectURL(blob);";
  html += "  let a=document.createElement('a');";
  html += "  a.href=url;a.download=filename;document.body.appendChild(a);a.click();document.body.removeChild(a);URL.revokeObjectURL(url);";
  html += "}";
  html += "</script>";
  html += "</body></html>";

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/html", html);
}

void handleNotFound() {
  server.send(404, "application/json", "{\"error\":\"endpoint non trovato\"}");
}

// === Serial command processing (invariato) ===

void processCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();
  if (cmd.length() == 0) return;
  bool changed = false;

  if (cmd.startsWith("T")) {
    float temp = cmd.substring(1).toFloat();
    if (temp >= 5.0 && temp <= 35.0) {
      regTemp = (uint16_t)(temp * 10);
      Serial.printf(">>> Temperatura: %.1f&deg;C\n", temp);
      changed = true;
    } else {
      Serial.println("!!! Range 5-35&deg;C");
    }
  }
  else if (cmd == "ON") {
    regConfig &= ~((1 << 14) | (1 << 13) | (1 << 7));  // clear mode + standby
    if (heating) regConfig |= (1 << 13); else regConfig |= (1 << 14);  // caldo=bit13, freddo=bit14
    powerOn = true; Serial.println(">>> ACCESO"); changed = true;
  }
  else if (cmd == "OFF") {
    regConfig |= (1 << 7);   // bit 7 = standby
    powerOn = false;
    sendAllRegisters();
    Serial.println(">>> SPENTO (bit 7 standby)");
  }
  else if (cmd.startsWith("FAN") && cmd.length() == 4) {
    int s = cmd.charAt(3) - '0';
    if (s >= 0 && s <= 3) { regConfig = (regConfig & ~0x03) | (s & 0x03); changed = true; }
  }
  else if (cmd == "HEAT") {
    heating = true; regMode |= 0x02;
    if (powerOn) { regConfig &= ~(1 << 14); regConfig |= (1 << 13); }  // caldo = bit 13
    changed = true;
  }
  else if (cmd == "COOL") {
    heating = false; regMode &= ~0x02;
    if (powerOn) { regConfig &= ~(1 << 13); regConfig |= (1 << 14); }  // freddo = bit 14
    changed = true;
  }
  else if (cmd.startsWith("R101 ")) {
    uint16_t v = (uint16_t)strtol(cmd.c_str() + 5, NULL, 0);
    regConfig = v;
    Serial.printf(">>> REG 101 = 0x%04X\n", v);
    changed = true;
  }
  else if (cmd.startsWith("R103 ")) {
    uint16_t v = (uint16_t)strtol(cmd.c_str() + 5, NULL, 0);
    regMode = v;
    Serial.printf(">>> REG 103 = 0x%04X\n", v);
    changed = true;
  }
  else if (cmd == "STATUS") {
    Serial.println(statusJSON());
  }
  else if (cmd == "SEND") { sendAllRegisters(); }
  else if (cmd == "IP") { Serial.println(WiFi.localIP()); }

  if (changed) {
    sendAllRegisters();
    Serial.println(statusJSON());
  }
}

// === Ascolto bus RS485 ===
#define MAX_FRAME_LEN 256
char frameBuf[MAX_FRAME_LEN];
int fPos = 0;
bool fActive = false;

void processRxByte(char c) {
  if (c == ':') { fPos = 0; fActive = true; }
  else if (c == '\r') {}
  else if (c == '\n' && fActive) {
    frameBuf[fPos] = '\0';
    if (fPos > 0) {
      Serial.printf("[BUS] :%s\n", frameBuf);

      // Cattura il frame nel buffer (sempre, sia in controllo che sniffer)
      SniffedFrame frame;
      if (parseModbusFrame(frameBuf, &frame)) {
        sniffBuffer[sniffIndex % SNIFFER_BUFFER_SIZE] = frame;
        sniffIndex++;
      }
    }
    fActive = false; fPos = 0;
  }
  else if (fActive && fPos < MAX_FRAME_LEN - 1) { frameBuf[fPos++] = c; }
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

  Serial.println("\n=============================================");
  Serial.println("  VISLA Controller + WiFi API");
  Serial.println("  Viessmann Energycal Slim W");
  Serial.println("=============================================\n");

  // RS485
  initRS485();

  // WiFi
  Serial.printf("Connessione WiFi: %s ", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWiFi connesso! IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Apri nel browser: http://%s\n\n", WiFi.localIP().toString().c_str());

    // Setup web server
    server.on("/", handleRoot);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/temperature", HTTP_POST, handleTemperature);
    server.on("/api/temperature", HTTP_GET, handleTemperature);  // anche GET per comodita'
    server.on("/api/power", HTTP_POST, handlePower);
    server.on("/api/power", HTTP_GET, handlePower);
    server.on("/api/fan", HTTP_POST, handleFan);
    server.on("/api/fan", HTTP_GET, handleFan);
    server.on("/api/mode", HTTP_POST, handleMode);
    server.on("/api/mode", HTTP_GET, handleMode);
    server.on("/api/reg", HTTP_GET, handleReg);
    server.on("/test", handleTest);
    server.on("/sniffer", handleSniffer);
    server.on("/api/reset-sniffer", HTTP_GET, handleResetSniffer);
    server.onNotFound(handleNotFound);
    server.begin();
    Serial.println("Web server avviato sulla porta 80");
#ifdef SNIFFER_MODE
    Serial.println("!!! SNIFFER MODE ATTIVO - ascolto passivo, nessun invio !!!");
#endif
  } else {
    Serial.println("\nWiFi non connesso. Funziona solo via USB seriale.");
    Serial.println("Modifica WIFI_SSID e WIFI_PASS nel codice.");
  }

  // Primo invio
  sendAllRegisters();
  lastSend = millis();
}

void loop() {
  // Web server
  server.handleClient();

  // Serial USB
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

  // RS485 bus
  while (RS485.available()) {
    uint8_t raw = RS485.read();
    processRxByte((char)(raw & 0x7F));
  }

  // Invio periodico (solo quando acceso)
  if (powerOn && millis() - lastSend >= SEND_INTERVAL) {
    lastSend = millis();
    sendAllRegisters();
  }
}
