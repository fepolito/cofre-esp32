/**
 * @file main.cpp
 * @brief Firmware de Controle para Retrofit de Cofre Eletrônico Inteligente
 * @author Fernando Polito
 * @mcu ESP32-C3FH4 (Pro Mini / Super Mini)
 * @features Varredor Matricial Nativo 3x4 com Imunidade RF (25µs settling delay),
 *           Debounce Triplo de 30ms, Auto-Clear de Inatividade (3.5s),
 *           Timeout de Inatividade com Entrada Automática em Deep Sleep,
 *           Proteção de Inicialização em Bateria (BOD Disable, Rampa 350ms, Watchdog),
 *           Modo Osciloscópio Contínuo de 60s (Web e Teclado 888P/777P),
 *           Histórico Auditado com Exibição de Dígitos Brutos no Canal,
 *           Correção de Underflow Monotônico nos Logs ("Sessão anterior"),
 *           Buzzer Multi-Modo (Ativo PNP DC LOW, Ativo NPN DC HIGH, Passivo AC PWM),
 *           Captive Portal Otimizado (+5 dBm RF, 80 MHz, GZIP).
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "web_page.h"

// ============================================================================
// MAPEAMENTO DE PINAGEM FÍSICA
// ============================================================================
const uint8_t PIN_SOLENOID   = 7;   // Solenoide (Darlington Q1/Q2 via R2 500Ω)
const uint8_t PIN_LED_GREEN  = 8;   // LED Verde (Active-LOW)
const uint8_t PIN_LED_RED    = 10;  // LED Vermelho (Active-LOW)

const uint8_t ROWS = 3;
const uint8_t COLS = 4;

const uint8_t ROW_PINS[ROWS] = {4, 5, 6};    // Linha 1 (Q3/Buzzer), Linha 2, Linha 3
const uint8_t COL_PINS[COLS] = {0, 1, 2, 3}; // Coluna 1, 2, 3, 4

const char KEY_MAP[ROWS][COLS] = {
  {'0', '1', '2', '3'}, // Linha 1 (GPIO 4)
  {'C', '4', '5', '6'}, // Linha 2 (GPIO 5)
  {'P', '7', '8', '9'}  // Linha 3 (GPIO 6)
};

// ============================================================================
// GESTÃO DE USUÁRIOS E CHAVE DE RESGATE MAC
// ============================================================================
struct SafeUser {
  String name;
  String pin;
  bool isAdmin;
};

const int MAX_USERS = 5;
SafeUser users[MAX_USERS];
int userCount = 0;

String deviceMac = "";
String emergencyRescuePin = "";

String calculateRescuePin(String mac) {
  String cleanMac = "";
  for (unsigned int i = 0; i < mac.length(); i++) {
    char c = mac[i];
    if (c != ':' && c != '-') cleanMac += c;
  }
  const char* salt = "COFRE_POLITO_SECURE_2026";
  uint32_t hashVal = 5381;
  for (unsigned int i = 0; i < cleanMac.length(); i++) {
    hashVal = ((hashVal << 5) + hashVal) + cleanMac[i];
  }
  for (unsigned int i = 0; i < strlen(salt); i++) {
    hashVal = ((hashVal << 5) + hashVal) + salt[i];
  }
  uint32_t pinNum = hashVal % 1000000;
  char buf[7];
  sprintf(buf, "%06u", pinNum);
  return String(buf);
}

// ============================================================================
// LOG DE AUDITORIA PERSISTENTE EM FLASH (NVS PREFERENCES)
// ============================================================================
struct AuditLog {
  String user;
  String method;
  String action;
  bool success;
  unsigned long timeSec;
};

const int MAX_LOGS = 12;
AuditLog logs[MAX_LOGS];
int logCount = 0;

Preferences prefs;
Preferences logPrefs;

void loadAuditLogs() {
  logPrefs.begin("cofre_logs", false);
  logCount = logPrefs.getInt("cnt", 0);
  if (logCount > MAX_LOGS) logCount = MAX_LOGS;

  for (int i = 0; i < logCount; i++) {
    logs[i].user    = logPrefs.getString(("u_" + String(i)).c_str(), "Sistema");
    logs[i].method  = logPrefs.getString(("m_" + String(i)).c_str(), "Auto");
    logs[i].action  = logPrefs.getString(("a_" + String(i)).c_str(), "Evento");
    logs[i].success = logPrefs.getBool(("s_" + String(i)).c_str(), true);
    logs[i].timeSec = logPrefs.getULong(("t_" + String(i)).c_str(), 0);
  }
  Serial.printf("[LOGS] %d eventos recuperados da Flash permanente.\n", logCount);
}

void saveSingleLogToFlash(int idx) {
  logPrefs.putString(("u_" + String(idx)).c_str(), logs[idx].user);
  logPrefs.putString(("m_" + String(idx)).c_str(), logs[idx].method);
  logPrefs.putString(("a_" + String(idx)).c_str(), logs[idx].action);
  logPrefs.putBool(("s_" + String(idx)).c_str(), logs[idx].success);
  logPrefs.putULong(("t_" + String(idx)).c_str(), logs[idx].timeSec);
}

void addLog(String user, String method, String action, bool success) {
  unsigned long nowSec = millis() / 1000;
  if (logCount < MAX_LOGS) {
    logs[logCount] = {user, method, action, success, nowSec};
    saveSingleLogToFlash(logCount);
    logCount++;
    logPrefs.putInt("cnt", logCount);
  } else {
    for (int i = 0; i < MAX_LOGS - 1; i++) {
      logs[i] = logs[i + 1];
      saveSingleLogToFlash(i);
    }
    logs[MAX_LOGS - 1] = {user, method, action, success, nowSec};
    saveSingleLogToFlash(MAX_LOGS - 1);
  }
}

// ============================================================================
// VARIÁVEIS GLOBAIS DE ESTADO
// ============================================================================
DNSServer dnsServer;
WebServer server(80);

uint16_t solenoidPulseMs = 800;
uint16_t portalTimeoutS  = 180;
uint8_t  buzzerMode      = 0; // 0 = DC LOW (PNP), 1 = DC HIGH (NPN), 2 = AC Tone (Passivo)

enum AppState { MODE_NORMAL, MODE_BLOCKED };
AppState appState = MODE_NORMAL;

String inputPinBuffer = "";
uint8_t failedAttempts = 0;
unsigned long blockedUntil = 0;
unsigned long lastActivityTime = 0;
unsigned long lastKeyPressTime = 0;
unsigned long solenoidOffTime = 0;
bool solenoidActive = false;
bool wifiActive = true;

// Modo Osciloscópio (Sinal Contínuo de 60 segundos)
enum ScopeMode { SCOPE_OFF, SCOPE_LOW, SCOPE_HIGH, SCOPE_TONE };
ScopeMode scopeMode = SCOPE_OFF;
unsigned long scopeEndTime = 0;

void stopScopeTest();

// ============================================================================
// CONTROLE DO BUZZER MULTI-MODO E LEDS
// ============================================================================
void soundBuzzer(uint16_t freq, uint16_t durationMs) {
  if (durationMs == 0) return;
  if (scopeMode != SCOPE_OFF) return; // Se teste de osciloscópio ativo, não interrompe

  pinMode(ROW_PINS[0], OUTPUT);

  uint8_t mode = buzzerMode;
  if (freq == 1) mode = 0;      // Força modo DC LOW (PNP)
  else if (freq == 2) mode = 1; // Força modo DC HIGH (NPN)
  else if (freq > 10) mode = 2; // Força modo AC PWM Tone

  if (mode == 0) {
    digitalWrite(ROW_PINS[0], LOW);
    delay(durationMs);
  } else if (mode == 1) {
    digitalWrite(ROW_PINS[0], HIGH);
    delay(durationMs);
  } else {
    uint16_t f = (freq > 10) ? freq : 2700;
    unsigned long halfPeriodUs = 500000UL / f;
    unsigned long cycles = ((unsigned long)f * durationMs) / 1000UL;
    for (unsigned long i = 0; i < cycles; i++) {
      digitalWrite(ROW_PINS[0], LOW);
      delayMicroseconds(halfPeriodUs);
      digitalWrite(ROW_PINS[0], HIGH);
      delayMicroseconds(halfPeriodUs);
    }
  }

  // CRUCIAL: Retorna Linha 1 para INPUT_PULLUP ao terminar!
  pinMode(ROW_PINS[0], INPUT_PULLUP);
}

void beepKey() {
  digitalWrite(PIN_LED_GREEN, LOW);
  soundBuzzer(0, 35);
  digitalWrite(PIN_LED_GREEN, HIGH);
}

void beepSuccess() {
  digitalWrite(PIN_LED_GREEN, LOW);
  soundBuzzer(0, 80);
  delay(40);
  soundBuzzer(0, 120);
  digitalWrite(PIN_LED_GREEN, HIGH);
}

void beepRescue() {
  digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_RED, LOW);
  soundBuzzer(0, 150);
  delay(50);
  soundBuzzer(0, 150);
  delay(50);
  soundBuzzer(0, 250);
  digitalWrite(PIN_LED_GREEN, HIGH);
  digitalWrite(PIN_LED_RED, HIGH);
}

void beepError() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_LED_RED, LOW);
    soundBuzzer(0, 100);
    digitalWrite(PIN_LED_RED, HIGH);
    delay(80);
  }
}

// ============================================================================
// MODO OSCILOSCÓPIO (SINAL CONTÍNUO DE 60 SEGUNDOS)
// ============================================================================
void startScopeTest(ScopeMode mode) {
  scopeMode = mode;
  scopeEndTime = millis() + 60000UL; // 60 segundos
  lastActivityTime = millis();

  pinMode(ROW_PINS[0], OUTPUT);
  digitalWrite(PIN_LED_RED, LOW); // LED Vermelho aceso indicando sinal contínuo ativo

  if (scopeMode == SCOPE_LOW) {
    digitalWrite(ROW_PINS[0], LOW);
    Serial.println("[OSCILOSCOPIO] Sinal DC LOW ativo no GPIO 4 (Base de Q3) por 60s.");
  } else if (scopeMode == SCOPE_HIGH) {
    digitalWrite(ROW_PINS[0], HIGH);
    Serial.println("[OSCILOSCOPIO] Sinal DC HIGH ativo no GPIO 4 (Base de Q3) por 60s.");
  } else if (scopeMode == SCOPE_TONE) {
    tone(ROW_PINS[0], 2700);
    Serial.println("[OSCILOSCOPIO] Tom PWM 2.7 kHz ativo no GPIO 4 por 60s.");
  }
}

void stopScopeTest() {
  if (scopeMode == SCOPE_TONE) {
    noTone(ROW_PINS[0]);
  }
  pinMode(ROW_PINS[0], INPUT_PULLUP);
  digitalWrite(PIN_LED_RED, HIGH); // Apaga LED Vermelho
  scopeMode = SCOPE_OFF;
  Serial.println("[OSCILOSCOPIO] Teste finalizado. GPIO 4 restaurado para INPUT_PULLUP.");
}

// ============================================================================
// ACIONAMENTO DO SOLENOIDE
// ============================================================================
void triggerSolenoid(String authorizedUser, String method, bool isRescue = false) {
  Serial.printf("[COFRE] Solenoide ATIVADO para '%s' via %s!\n", authorizedUser.c_str(), method.c_str());
  digitalWrite(PIN_SOLENOID, HIGH);
  digitalWrite(PIN_LED_GREEN, LOW);
  solenoidActive = true;
  solenoidOffTime = millis() + solenoidPulseMs;

  addLog(authorizedUser, method, isRescue ? "Resgate Chave MAC" : "Abertura autorizada", true);

  if (isRescue) beepRescue(); else beepSuccess();
}

void updateSolenoid() {
  if (solenoidActive && millis() >= solenoidOffTime) {
    digitalWrite(PIN_SOLENOID, LOW);
    digitalWrite(PIN_LED_GREEN, HIGH);
    solenoidActive = false;
  }
}

// ============================================================================
// DEEP SLEEP (REPOUSO DE BATERIA)
// ============================================================================
void enterDeepSleep() {
  Serial.println("[ENERGIA] Desligando Wi-Fi e entrando em repouso...");
  stopScopeTest();
  esp_task_wdt_delete(NULL); // Remove watchdog antes do sono

  digitalWrite(PIN_SOLENOID, LOW);
  digitalWrite(PIN_LED_GREEN, HIGH);
  digitalWrite(PIN_LED_RED, HIGH);

  gpio_hold_en((gpio_num_t)PIN_LED_GREEN);
  gpio_hold_en((gpio_num_t)PIN_LED_RED);

  for (int r = 0; r < ROWS; r++) {
    pinMode(ROW_PINS[r], OUTPUT);
    digitalWrite(ROW_PINS[r], LOW);
    gpio_hold_en((gpio_num_t)ROW_PINS[r]);
  }
  gpio_deep_sleep_hold_en();

  for (int c = 0; c < COLS; c++) {
    pinMode(COL_PINS[c], INPUT_PULLUP);
    esp_deep_sleep_enable_gpio_wakeup(1ULL << COL_PINS[c], ESP_GPIO_WAKEUP_GPIO_LOW);
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.flush();
  esp_deep_sleep_start();
}

// ============================================================================
// ROTAS REST DO CAPTIVE PORTAL
// ============================================================================
void handleRoot() {
  lastActivityTime = millis();
  server.sendHeader("Content-Encoding", "gzip");
  server.send_P(200, "text/html", (const char*)INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
}

void handleNotFound() {
  if (server.uri().startsWith("/api/")) {
    server.send(404, "application/json", "{\"status\":\"error\",\"message\":\"Rota inexistente\"}");
  } else {
    server.sendHeader("Content-Encoding", "gzip");
    server.send_P(200, "text/html", (const char*)INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
  }
}

void handleApiStatus() {
  lastActivityTime = millis();
  String json = "{";
  json += "\"status\":\"ok\",";
  json += "\"pulseMs\":" + String(solenoidPulseMs) + ",";
  json += "\"sleepTimeout\":" + String(portalTimeoutS) + ",";
  json += "\"buzzerMode\":" + String(buzzerMode) + ",";
  json += "\"batteryV\":9.0,";
  json += "\"deviceMac\":\"" + deviceMac + "\",";
  json += "\"isDefaultMaster\":" + String(users[0].pin == "123456" ? "true" : "false") + ",";

  json += "\"users\":[";
  for (int i = 0; i < userCount; i++) {
    json += "{\"id\":" + String(i + 1) + ",";
    json += "\"name\":\"" + users[i].name + "\",";
    json += "\"role\":\"" + String(users[i].isAdmin ? "admin" : "user") + "\"}";
    if (i < userCount - 1) json += ",";
  }
  json += "],";

  json += "\"logs\":[";
  unsigned long currentSec = millis() / 1000;
  for (int i = logCount - 1; i >= 0; i--) {
    json += "{\"user\":\"" + logs[i].user + "\",";
    json += "\"method\":\"" + logs[i].method + "\",";
    json += "\"action\":\"" + logs[i].action + "\",";
    json += "\"success\":" + String(logs[i].success ? "true" : "false") + ",";

    // Correção do Underflow Monotônico de 32 bits
    String timeStr;
    if (currentSec >= logs[i].timeSec) {
      unsigned long s = currentSec - logs[i].timeSec;
      timeStr = s < 60 ? (String(s) + "s atrás") : (String(s / 60) + "m atrás");
    } else {
      timeStr = "Sessão anterior";
    }

    json += "\"time\":\"" + timeStr + "\"}";
    if (i > 0) json += ",";
  }
  json += "]}";

  server.send(200, "application/json", json);
}

void handleApiUnlock() {
  lastActivityTime = millis();
  String pin = server.arg("pin");
  pin.trim();

  Serial.printf("[AUTH WEB] Tentativa com PIN: %s\n", pin.c_str());

  if (pin.length() >= 4 && pin == emergencyRescuePin) {
    failedAttempts = 0;
    triggerSolenoid("RESCUE (Chave MAC)", "Web Portal", true);
    server.send(200, "application/json", "{\"status\":\"ok\",\"isRescue\":true,\"message\":\"DESTRAVADO VIA CHAVE DE RESGATE!\"}");
    return;
  }

  for (int i = 0; i < userCount; i++) {
    if (pin.length() >= 4 && pin == users[i].pin) {
      failedAttempts = 0;
      triggerSolenoid(users[i].name, "Web Portal", false);
      server.send(200, "application/json", "{\"status\":\"ok\",\"isRescue\":false,\"userName\":\"" + users[i].name + "\",\"message\":\"Acesso Liberado!\"}");
      return;
    }
  }

  failedAttempts++;
  addLog("Desconhecido", "Web Portal", "Tentativa de senha incorreta", false);
  beepError();
  server.send(401, "application/json", "{\"status\":\"error\",\"message\":\"Senha incorreta! Acesso negado.\"}");
}

void handleApiChangeMaster() {
  lastActivityTime = millis();
  String name   = server.arg("name");
  String oldPin = server.arg("oldPin");
  String newPin = server.arg("newPin");
  name.trim(); oldPin.trim(); newPin.trim();

  if (name.length() == 0) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"O nome do Mestre não pode ser vazio!\"}");
    return;
  }

  if (oldPin != users[0].pin && oldPin != emergencyRescuePin) {
    server.send(403, "application/json", "{\"status\":\"error\",\"message\":\"Senha Mestre atual incorreta!\"}");
    beepError();
    return;
  }

  users[0].name = name;
  prefs.putString("u_name_0", name);

  if (newPin.length() >= 4) {
    users[0].pin = newPin;
    prefs.putString("u_pin_0", newPin);
    Serial.printf("[AUTH] Senha Mestre de '%s' atualizada para: %s\n", name.c_str(), newPin.c_str());
  } else {
    Serial.printf("[AUTH] Nome do Mestre atualizado para '%s'\n", name.c_str());
  }

  addLog(users[0].name, "Web Portal", "Dados do Mestre atualizados", true);
  server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Dados do Administrador Mestre salvos com sucesso!\"}");
  beepSuccess();
}

void handleApiDeleteUser() {
  lastActivityTime = millis();
  int id = server.arg("id").toInt();
  String adminPin = server.arg("adminPin");
  adminPin.trim();

  if (adminPin != users[0].pin && adminPin != emergencyRescuePin) {
    server.send(403, "application/json", "{\"status\":\"error\",\"message\":\"Senha Mestre incorreta!\"}");
    return;
  }

  int idx = id - 1;
  if (idx <= 0 || idx >= userCount) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Usuario invalido ou nao permitido excluir mestre!\"}");
    return;
  }

  String deletedName = users[idx].name;
  for (int i = idx; i < userCount - 1; i++) {
    users[i] = users[i + 1];
    prefs.putString(("u_name_" + String(i)).c_str(), users[i].name);
    prefs.putString(("u_pin_" + String(i)).c_str(), users[i].pin);
  }
  userCount--;
  prefs.putInt("u_cnt", userCount);

  addLog("Admin", "Web Portal", "Usuario excluido: " + deletedName, true);
  server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Usuario excluido com sucesso!\"}");
}

void handleApiEmergencyReset() {
  lastActivityTime = millis();
  String rescuePin = server.arg("rescuePin");
  rescuePin.trim();

  if (rescuePin == emergencyRescuePin) {
    users[0].pin = "123456";
    prefs.putString("u_pin_0", "123456");
    addLog("RESCUE (Chave MAC)", "Web Portal", "Senha Mestre restaurada para 123456", true);
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Senha Mestre restaurada para 123456!\"}");
    beepSuccess();
  } else {
    server.send(403, "application/json", "{\"status\":\"error\",\"message\":\"Chave de Resgate MAC incorreta!\"}");
    beepError();
  }
}

void handleApiAddUser() {
  lastActivityTime = millis();
  String name     = server.arg("name");
  String pin      = server.arg("pin");
  String adminPin = server.arg("adminPin");
  name.trim(); pin.trim(); adminPin.trim();

  if (adminPin != users[0].pin && adminPin != emergencyRescuePin) {
    server.send(403, "application/json", "{\"status\":\"error\",\"message\":\"Senha Mestre incorreta! Não autorizado.\"}");
    return;
  }

  if (userCount >= MAX_USERS) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Limite máximo de 5 usuários atingido!\"}");
    return;
  }

  users[userCount] = {name, pin, false};
  userCount++;
  prefs.putInt("u_cnt", userCount);
  prefs.putString(("u_name_" + String(userCount - 1)).c_str(), name);
  prefs.putString(("u_pin_" + String(userCount - 1)).c_str(), pin);

  addLog("Admin", "Web Portal", "Novo usuário adicional: " + name, true);
  server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Usuário adicional cadastrado com sucesso!\"}");
}

void handleApiConfig() {
  lastActivityTime = millis();
  if (server.hasArg("pulse")) {
    solenoidPulseMs = server.arg("pulse").toInt();
    prefs.putUShort("pulse", solenoidPulseMs);
  }
  if (server.hasArg("timeout")) {
    portalTimeoutS = server.arg("timeout").toInt();
    prefs.putUShort("timeout", portalTimeoutS);
  }
  if (server.hasArg("buzzerMode")) {
    buzzerMode = server.arg("buzzerMode").toInt();
    prefs.putUChar("bz_mode", buzzerMode);
    Serial.printf("[BUZZER] Modo padrao alterado para: %d\n", buzzerMode);
  }
  server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Configurações salvas com sucesso!\"}");
}

void handleApiTestBuzzer() {
  lastActivityTime = millis();
  String mode = server.arg("mode");
  mode.trim(); mode.toLowerCase();

  if (mode == "scope_low") {
    startScopeTest(SCOPE_LOW);
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Modo Osciloscópio: Sinal DC LOW contínuo por 60s ativado!\"}");
  } else if (mode == "scope_high") {
    startScopeTest(SCOPE_HIGH);
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Modo Osciloscópio: Sinal DC HIGH contínuo por 60s ativado!\"}");
  } else if (mode == "scope_tone") {
    startScopeTest(SCOPE_TONE);
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Modo Osciloscópio: Onda Quadrada 2.7kHz contínua por 60s ativada!\"}");
  } else if (mode == "stop") {
    stopScopeTest();
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Teste interrompido. GPIO 4 liberado!\"}");
  } else if (mode == "low") {
    soundBuzzer(1, 100);
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Pulso DC LOW (100ms) emitido no Buzzer!\"}");
  } else if (mode == "high") {
    soundBuzzer(2, 100);
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Pulso DC HIGH (100ms) emitido no Buzzer!\"}");
  } else {
    soundBuzzer(2700, 150);
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Tom AC PWM 2.7 kHz (150ms) emitido no Buzzer!\"}");
  }
}

void handleApiSleep() {
  server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Entrando em modo repouso...\"}");
  delay(500);
  enterDeepSleep();
}

void startWiFiPortal() {
  Serial.println("[WIFI] Ativando rede 'Cofre-Smart-Setup' com RF Power otimizado (5 dBm)...");
  WiFi.mode(WIFI_AP);
  WiFi.setTxPower(WIFI_POWER_5dBm);
  WiFi.softAP("Cofre-Smart-Setup");

  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  dnsServer.start(53, "*", apIP);

  server.on("/", handleRoot);
  server.on("/api/status", handleApiStatus);
  server.on("/api/unlock", handleApiUnlock);
  server.on("/api/change_master", handleApiChangeMaster);
  server.on("/api/delete_user", handleApiDeleteUser);
  server.on("/api/emergency_reset", handleApiEmergencyReset);
  server.on("/api/add_user", handleApiAddUser);
  server.on("/api/config", handleApiConfig);
  server.on("/api/test_buzzer", handleApiTestBuzzer);
  server.on("/api/sleep", handleApiSleep);
  server.onNotFound(handleNotFound);

  server.begin();
  wifiActive = true;
  lastActivityTime = millis();
}

// ============================================================================
// PROCESSAMENTO DO TECLADO FÍSICO COM BUFFER VISÍVEL E AUTO-CLEAR
// ============================================================================
void processKeyPress(char key) {
  lastActivityTime = millis();
  lastKeyPressTime = millis();

  // Se teste de osciloscópio estiver rodando e qualquer tecla for pressionada, interrompe
  if (scopeMode != SCOPE_OFF && key == 'C') {
    stopScopeTest();
    inputPinBuffer = "";
    return;
  }

  if (key == 'P') {
    Serial.printf("[TECLADO] Tecla P pressionada. Buffer: '%s'\n", inputPinBuffer.c_str());

    String rawBuffer = inputPinBuffer;
    String channelStr = "Teclado [" + (rawBuffer.length() > 0 ? rawBuffer : "vazio") + "]";

    // Comando do Osciloscópio via Teclado: "888P" (LOW) ou "777P" (Tom)
    if (rawBuffer == "888" || rawBuffer.endsWith("888")) {
      startScopeTest(SCOPE_LOW);
      addLog("Fernando", channelStr, "Osciloscópio 60s (GPIO 4 LOW)", true);
      inputPinBuffer = "";
      return;
    }
    if (rawBuffer == "777" || rawBuffer.endsWith("777")) {
      startScopeTest(SCOPE_TONE);
      addLog("Fernando", channelStr, "Osciloscópio 60s (Tom 2.7kHz)", true);
      inputPinBuffer = "";
      return;
    }

    // Comando para ligar Wi-Fi: "000" ou termina com "000" ou apenas zeros (>= 3)
    bool isWebCode = (rawBuffer == "000" || rawBuffer.endsWith("000") || rawBuffer == "C000");
    if (!isWebCode && rawBuffer.length() >= 3) {
      isWebCode = true;
      for (size_t i = 0; i < rawBuffer.length(); i++) {
        if (rawBuffer[i] != '0') { isWebCode = false; break; }
      }
    }

    if (isWebCode) {
      startWiFiPortal();
      addLog("Sistema", channelStr, "Wi-Fi Ativado via Teclado (000P)", true);
      beepSuccess();
      inputPinBuffer = "";
      return;
    }

    // Chave de Resgate MAC
    if (rawBuffer.length() >= 4 && (rawBuffer == emergencyRescuePin || rawBuffer.endsWith(emergencyRescuePin))) {
      failedAttempts = 0;
      triggerSolenoid("RESCUE (Chave MAC)", channelStr, true);
      inputPinBuffer = "";
      return;
    }

    // Usuários cadastrados (Mestre ou Adicionais)
    bool authenticated = false;
    for (int i = 0; i < userCount; i++) {
      if (users[i].pin.length() >= 4 && (rawBuffer == users[i].pin || rawBuffer.endsWith(users[i].pin))) {
        authenticated = true;
        failedAttempts = 0;
        triggerSolenoid(users[i].name, channelStr, false);
        break;
      }
    }

    if (!authenticated) {
      Serial.printf("[AUTH TECLADO] Senha incorreta! Buffer: '%s'\n", rawBuffer.c_str());
      failedAttempts++;
      addLog("Desconhecido", channelStr, "Senha incorreta: [" + rawBuffer + "]", false);
      beepError();

      if (failedAttempts >= 5) {
        appState = MODE_BLOCKED;
        blockedUntil = millis() + 300000;
      } else if (failedAttempts >= 3) {
        appState = MODE_BLOCKED;
        blockedUntil = millis() + 60000;
      }
    }

    inputPinBuffer = "";
  }
  else if (key == 'C') {
    if (scopeMode != SCOPE_OFF) {
      stopScopeTest();
    }
    inputPinBuffer = "";
    digitalWrite(PIN_LED_RED, LOW);
    soundBuzzer(0, 60);
    digitalWrite(PIN_LED_RED, HIGH);
    Serial.println("[TECLADO] Buffer limpo manualmente (C).");
  }
  else {
    if (inputPinBuffer.length() < 16) {
      inputPinBuffer += key;
      Serial.printf("[TECLADO] Digito '%c' capturado. Buffer: '%s'\n", key, inputPinBuffer.c_str());
    }
  }
}

// ============================================================================
// VARREDOR MATRICIAL NATIVO DE ALTA IMUNIDADE A RUÍDO RF (TEMPO DE ACOMODAÇÃO)
// ============================================================================
char scanRawKey() {
  for (int r = 0; r < ROWS; r++) {
    pinMode(ROW_PINS[r], INPUT_PULLUP);
  }
  char pressed = 0;
  for (int c = 0; c < COLS; c++) {
    pinMode(COL_PINS[c], OUTPUT);
    digitalWrite(COL_PINS[c], LOW);
    delayMicroseconds(25); // 25µs de acomodação para o cabo da porta e ruído RF
    for (int r = 0; r < ROWS; r++) {
      if (digitalRead(ROW_PINS[r]) == LOW) {
        pressed = KEY_MAP[r][c];
      }
    }
    digitalWrite(COL_PINS[c], HIGH);
    pinMode(COL_PINS[c], INPUT_PULLUP);
    if (pressed != 0) break;
  }
  return pressed;
}

void updateKeypad() {
  static char lastRawKey = 0;
  static char confirmedKey = 0;
  static unsigned long lastScanMs = 0;
  static uint8_t stableCount = 0;

  if (millis() - lastScanMs < 10) return;
  lastScanMs = millis();

  char raw = scanRawKey();
  if (raw == lastRawKey && raw != 0) {
    stableCount++;
    if (stableCount == 3 && confirmedKey == 0) { // 30ms de debounce estável
      confirmedKey = raw;
      processKeyPress(confirmedKey);
    }
  } else if (raw == 0) {
    if (confirmedKey != 0) {
      if (confirmedKey != 'P' && confirmedKey != 'C') {
        beepKey();
      }
      confirmedKey = 0;
    }
    stableCount = 0;
    lastRawKey = 0;
  } else {
    lastRawKey = raw;
    stableCount = 1;
  }

  // Auto-Clear de Inatividade: limpa buffer após 3.5 segundos sem teclas
  if (inputPinBuffer.length() > 0 && (millis() - lastKeyPressTime > 3500)) {
    Serial.printf("[TECLADO] Inatividade > 3.5s. Buffer '%s' limpo automaticamente.\n", inputPinBuffer.c_str());
    inputPinBuffer = "";
  }
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  // 1. Desabilita o Brownout Detector no início para evitar loops de reset
  // causados pela rampa de tensão do capacitor de 2200µF na bateria de 9V
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // 2. Pausa para estabilização de tensão da fonte/bateria (2200µF carregando)
  delay(350);

  setCpuFrequencyMhz(80);

  gpio_hold_dis((gpio_num_t)PIN_LED_GREEN);
  gpio_hold_dis((gpio_num_t)PIN_LED_RED);
  gpio_hold_dis((gpio_num_t)ROW_PINS[0]);
  gpio_hold_dis((gpio_num_t)ROW_PINS[1]);
  gpio_hold_dis((gpio_num_t)ROW_PINS[2]);
  gpio_deep_sleep_hold_dis();

  Serial.begin(115200);
  delay(50);

  Serial.println("\n\n==========================================");
  Serial.println("  COFRE INTELIGENTE ESP32-C3 - VERSÃO 4.2");
  Serial.println("==========================================");

  // Inicializa o Hardware Task Watchdog (6 segundos de tolerância)
  esp_task_wdt_init(6, true);
  esp_task_wdt_add(NULL);

  pinMode(PIN_SOLENOID, OUTPUT);
  digitalWrite(PIN_SOLENOID, LOW);

  pinMode(PIN_LED_GREEN, OUTPUT);
  digitalWrite(PIN_LED_GREEN, HIGH);

  pinMode(PIN_LED_RED, OUTPUT);
  digitalWrite(PIN_LED_RED, HIGH);

  for (int r = 0; r < ROWS; r++) {
    pinMode(ROW_PINS[r], INPUT_PULLUP);
  }
  for (int c = 0; c < COLS; c++) {
    pinMode(COL_PINS[c], INPUT_PULLUP);
  }

  deviceMac = WiFi.macAddress();
  emergencyRescuePin = calculateRescuePin(deviceMac);
  Serial.printf("[HARDWARE] MAC: %s | Chave Resgate: %s\n", deviceMac.c_str(), emergencyRescuePin.c_str());

  prefs.begin("cofre", false);
  solenoidPulseMs = prefs.getUShort("pulse", 800);
  portalTimeoutS  = prefs.getUShort("timeout", 180);
  buzzerMode      = prefs.getUChar("bz_mode", 0);
  userCount       = prefs.getInt("u_cnt", 0);

  if (userCount == 0) {
    users[0] = {"Fernando (Mestre)", "123456", true};
    userCount = 1;
    prefs.putInt("u_cnt", userCount);
    prefs.putString("u_name_0", users[0].name);
    prefs.putString("u_pin_0", users[0].pin);
    addLog("Sistema", "Sistema", "Inicialização de Fábrica", true);
  } else {
    for (int i = 0; i < userCount; i++) {
      users[i].name = prefs.getString(("u_name_" + String(i)).c_str(), "Usuario");
      users[i].pin  = prefs.getString(("u_pin_" + String(i)).c_str(), "1234");
      users[i].isAdmin = (i == 0);
    }
  }

  // Ajuste do Nome do Master se ficou como "Ivone"
  if (users[0].name == "Ivone") {
    users[0].name = "Fernando (Mestre)";
    prefs.putString("u_name_0", users[0].name);
    Serial.println("[PREFS] Nome do Administrador Mestre restaurado para 'Fernando (Mestre)'.");
  }

  // Carrega histórico auditado da Flash
  loadAuditLogs();

  for (int i = 0; i < userCount; i++) {
    Serial.printf("[USUARIO %d] %s | PIN: %s | Admin: %d\n", i, users[i].name.c_str(), users[i].pin.c_str(), users[i].isAdmin);
  }

  // Pisca LEDs Verde e Vermelho de Boas-Vindas
  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_LED_GREEN, LOW);
    digitalWrite(PIN_LED_RED, HIGH);
    delay(80);
    digitalWrite(PIN_LED_GREEN, HIGH);
    digitalWrite(PIN_LED_RED, LOW);
    delay(80);
  }
  digitalWrite(PIN_LED_GREEN, HIGH);
  digitalWrite(PIN_LED_RED, HIGH);

  // Diagnóstico Sonoro dos 3 modos no Boot
  Serial.println("[BUZZER] Executando diagnóstico sonoro dos 3 modos...");
  digitalWrite(PIN_LED_GREEN, LOW);
  soundBuzzer(1, 100); // Teste 1: DC LOW (Ativo PNP)
  digitalWrite(PIN_LED_GREEN, HIGH);
  delay(200);

  digitalWrite(PIN_LED_RED, LOW);
  soundBuzzer(2, 100); // Teste 2: DC HIGH (Ativo NPN)
  digitalWrite(PIN_LED_RED, HIGH);
  delay(200);

  digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_RED, LOW);
  soundBuzzer(2700, 150); // Teste 3: Tom AC PWM 2.7 kHz (Passivo)
  digitalWrite(PIN_LED_GREEN, HIGH);
  digitalWrite(PIN_LED_RED, HIGH);

  startWiFiPortal();
  lastActivityTime = millis();
  lastKeyPressTime = millis();
}

// ============================================================================
// LOOP PRINCIPAL
// ============================================================================
void loop() {
  esp_task_wdt_reset(); // Alimenta o Hardware Watchdog Timer
  updateSolenoid();

  if (wifiActive) {
    dnsServer.processNextRequest();
    server.handleClient();

    // Verificação de Timeout do Captive Portal para Economia de Bateria
    if (portalTimeoutS > 0 && (millis() - lastActivityTime > (unsigned long)portalTimeoutS * 1000UL)) {
      Serial.printf("[ENERGIA] Inatividade de %d segundos atingida. Entrando em Deep Sleep...\n", portalTimeoutS);
      addLog("Sistema", "Auto", "Timeout Inatividade (" + String(portalTimeoutS) + "s)", true);
      enterDeepSleep();
    }
  }

  // Atualização do Modo Osciloscópio (encerra automaticamente após 60s se ativo)
  if (scopeMode != SCOPE_OFF && millis() >= scopeEndTime) {
    stopScopeTest();
  }

  if (appState == MODE_BLOCKED) {
    digitalWrite(PIN_LED_RED, (millis() / 500) % 2 == 0 ? LOW : HIGH);
    if (millis() >= blockedUntil) {
      digitalWrite(PIN_LED_RED, HIGH);
      appState = MODE_NORMAL;
      failedAttempts = 0;
    }
    return;
  }

  // Varredura Nativa do Teclado com Acomodação RF e Debounce
  updateKeypad();

  // Heartbeat discreto do LED Verde (indica sistema rodando)
  static unsigned long lastHeartbeat = 0;
  if (!solenoidActive && scopeMode == SCOPE_OFF && (millis() - lastHeartbeat > 2000)) {
    lastHeartbeat = millis();
    digitalWrite(PIN_LED_GREEN, LOW);
    delay(15);
    digitalWrite(PIN_LED_GREEN, HIGH);
  }
}
