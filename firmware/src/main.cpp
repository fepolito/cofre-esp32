/**
 * @file main.cpp
 * @brief Firmware de Controle para Retrofit de Cofre Eletrônico Inteligente
 * @author Fernando Polito
 * @mcu ESP32-C3FH4 (Pro Mini / Super Mini)
 * @features Teclado Matricial 3x4, Captive Portal Wi-Fi com Baixa Potência RF (Economia de Bateria),
 *           Compressão GZIP, Gestão Dinâmica de Senha Mestre, Eliminação de 123456,
 *           LEDs Verde/Vermelho, Buzzer PNP compartilhado, Múltiplos Usuários em Flash,
 *           Log Auditado e Chave Mestre de Resgate por MAC.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <Keypad.h>
#include "web_page.h"

// ============================================================================
// MAPEAMENTO DE PINAGEM FÍSICA
// ============================================================================
const uint8_t PIN_SOLENOID   = 7;   // Solenoide (Darlington Q1/Q2 via R2 500Ω)
const uint8_t PIN_LED_GREEN  = 8;   // LED Verde (Active-LOW)
const uint8_t PIN_LED_RED    = 10;  // LED Vermelho (Active-LOW)

const byte ROWS = 3;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'0', '1', '2', '3'},       // Linha 1 (compartilhada c/ Buzzer Q3)
  {'C', '4', '5', '6'},       // Linha 2 (C = CLEAR)
  {'P', '7', '8', '9'}        // Linha 3 (P = PROGRAM / ENTER)
};

byte rowPins[ROWS] = {4, 5, 6};
byte colPins[COLS] = {0, 1, 2, 3};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

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

// Log de Auditoria
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

void addLog(String user, String method, String action, bool success) {
  if (logCount < MAX_LOGS) {
    logs[logCount] = {user, method, action, success, millis() / 1000};
    logCount++;
  } else {
    for (int i = 0; i < MAX_LOGS - 1; i++) logs[i] = logs[i + 1];
    logs[MAX_LOGS - 1] = {user, method, action, success, millis() / 1000};
  }
}

// ============================================================================
// VARIÁVEIS GLOBAIS DE ESTADO
// ============================================================================
Preferences prefs;
DNSServer dnsServer;
WebServer server(80);

uint16_t solenoidPulseMs = 800;
uint16_t portalTimeoutS  = 180;

enum AppState { MODE_NORMAL, MODE_BLOCKED };
AppState appState = MODE_NORMAL;

String inputPinBuffer = "";
uint8_t failedAttempts = 0;
unsigned long blockedUntil = 0;
unsigned long lastActivityTime = 0;
unsigned long solenoidOffTime = 0;
bool solenoidActive = false;
bool wifiActive = true;

// ============================================================================
// CONTROLE DO BUZZER E LEDS
// ============================================================================
void soundBuzzer(uint16_t freq, uint16_t durationMs) {
  if (freq == 0) return;
  unsigned long halfPeriodUs = 500000UL / freq;
  unsigned long cycles = ((unsigned long)freq * durationMs) / 1000UL;

  pinMode(rowPins[0], OUTPUT);
  for (unsigned long i = 0; i < cycles; i++) {
    digitalWrite(rowPins[0], LOW);  // Liga PNP Q3
    delayMicroseconds(halfPeriodUs);
    digitalWrite(rowPins[0], HIGH); // Desliga PNP Q3
    delayMicroseconds(halfPeriodUs);
  }
  digitalWrite(rowPins[0], HIGH);
}

void beepKey() {
  digitalWrite(PIN_LED_GREEN, LOW);
  soundBuzzer(2700, 35);
  digitalWrite(PIN_LED_GREEN, HIGH);
}

void beepSuccess() {
  digitalWrite(PIN_LED_GREEN, LOW);
  soundBuzzer(2000, 80);
  delay(40);
  soundBuzzer(2800, 120);
  digitalWrite(PIN_LED_GREEN, HIGH);
}

void beepRescue() {
  digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_RED, LOW);
  soundBuzzer(1800, 150);
  delay(50);
  soundBuzzer(2400, 150);
  delay(50);
  soundBuzzer(3200, 250);
  digitalWrite(PIN_LED_GREEN, HIGH);
  digitalWrite(PIN_LED_RED, HIGH);
}

void beepError() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_LED_RED, LOW);
    soundBuzzer(800, 120);
    digitalWrite(PIN_LED_RED, HIGH);
    delay(80);
  }
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
  digitalWrite(PIN_SOLENOID, LOW);
  digitalWrite(PIN_LED_GREEN, HIGH);
  digitalWrite(PIN_LED_RED, HIGH);

  for (int r = 0; r < ROWS; r++) {
    pinMode(rowPins[r], OUTPUT);
    digitalWrite(rowPins[r], LOW);
    gpio_hold_en((gpio_num_t)rowPins[r]);
  }
  gpio_deep_sleep_hold_en();

  for (int c = 0; c < COLS; c++) {
    pinMode(colPins[c], INPUT_PULLUP);
    esp_deep_sleep_enable_gpio_wakeup(1ULL << colPins[c], ESP_GPIO_WAKEUP_GPIO_LOW);
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
  for (int i = logCount - 1; i >= 0; i--) {
    json += "{\"user\":\"" + logs[i].user + "\",";
    json += "\"method\":\"" + logs[i].method + "\",";
    json += "\"action\":\"" + logs[i].action + "\",";
    json += "\"success\":" + String(logs[i].success ? "true" : "false") + ",";
    unsigned long s = (millis() / 1000) - logs[i].timeSec;
    String timeStr = s < 60 ? (String(s) + "s atrás") : (String(s / 60) + "m atrás");
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

void handleApiChangeMasterPin() {
  lastActivityTime = millis();
  String oldPin = server.arg("oldPin");
  String newPin = server.arg("newPin");
  oldPin.trim(); newPin.trim();

  if (newPin.length() < 4) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"O novo PIN deve ter pelo menos 4 digitos!\"}");
    return;
  }

  if (oldPin != users[0].pin && oldPin != emergencyRescuePin) {
    server.send(403, "application/json", "{\"status\":\"error\",\"message\":\"Senha Mestre atual incorreta!\"}");
    beepError();
    return;
  }

  users[0].pin = newPin;
  prefs.putString("u_pin_0", newPin);
  addLog(users[0].name, "Web Portal", "Senha Mestre alterada com sucesso", true);
  Serial.printf("[AUTH] Senha Mestre alterada para: %s (123456 eliminada!)\n", newPin.c_str());

  server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Senha Mestre atualizada com sucesso! Senha 123456 desativada.\"}");
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
  server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Configurações salvas!\"}");
}

void handleApiSleep() {
  server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Entrando em modo repouso...\"}");
  delay(500);
  enterDeepSleep();
}

void startWiFiPortal() {
  Serial.println("[WIFI] Ativando rede 'Cofre-Smart-Setup' com RF Power otimizado (5 dBm)...");
  WiFi.mode(WIFI_AP);
  WiFi.setTxPower(WIFI_POWER_5dBm); // 5dBm reduz pico de corrente em ~60% protegendo a bateria 9V
  WiFi.softAP("Cofre-Smart-Setup");

  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  dnsServer.start(53, "*", apIP);

  server.on("/", handleRoot);
  server.on("/api/status", handleApiStatus);
  server.on("/api/unlock", handleApiUnlock);
  server.on("/api/change_master_pin", handleApiChangeMasterPin);
  server.on("/api/delete_user", handleApiDeleteUser);
  server.on("/api/emergency_reset", handleApiEmergencyReset);
  server.on("/api/add_user", handleApiAddUser);
  server.on("/api/config", handleApiConfig);
  server.on("/api/sleep", handleApiSleep);
  server.onNotFound(handleNotFound);

  server.begin();
  wifiActive = true;
  lastActivityTime = millis();
}

// ============================================================================
// TECLADO FÍSICO (3x4)
// ============================================================================
void processKey(char key) {
  lastActivityTime = millis();
  beepKey();

  if (key == 'P') {
    Serial.printf("[TECLADO] Processando Buffer: '%s'\n", inputPinBuffer.c_str());

    if (inputPinBuffer == "000" || inputPinBuffer == "C000") {
      startWiFiPortal();
      beepSuccess();
      inputPinBuffer = "";
      return;
    }

    if (inputPinBuffer.length() >= 4 && inputPinBuffer == emergencyRescuePin) {
      failedAttempts = 0;
      triggerSolenoid("RESCUE (Chave MAC)", "Teclado", true);
      inputPinBuffer = "";
      return;
    }

    bool authenticated = false;
    for (int i = 0; i < userCount; i++) {
      if (inputPinBuffer.length() >= 4 && inputPinBuffer == users[i].pin) {
        authenticated = true;
        failedAttempts = 0;
        triggerSolenoid(users[i].name, "Teclado", false);
        break;
      }
    }

    if (!authenticated) {
      Serial.println("[AUTH TECLADO] Senha incorreta!");
      failedAttempts++;
      addLog("Desconhecido", "Teclado", "Senha inválida: " + inputPinBuffer, false);
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
    inputPinBuffer = "";
    digitalWrite(PIN_LED_RED, LOW);
    soundBuzzer(1200, 60);
    digitalWrite(PIN_LED_RED, HIGH);
  }
  else {
    if (inputPinBuffer.length() < 8) {
      inputPinBuffer += key;
    }
  }
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  setCpuFrequencyMhz(80); // Reduz clock de 160MHz para 80MHz (reduz consumo digital pela metade)

  gpio_hold_dis((gpio_num_t)rowPins[0]);
  gpio_hold_dis((gpio_num_t)rowPins[1]);
  gpio_hold_dis((gpio_num_t)rowPins[2]);
  gpio_deep_sleep_hold_dis();

  Serial.begin(115200);
  delay(100);

  Serial.println("\n\n==========================================");
  Serial.println("  COFRE INTELIGENTE ESP32-C3 - PRODUÇÃO");
  Serial.println("==========================================");

  pinMode(PIN_SOLENOID, OUTPUT);
  digitalWrite(PIN_SOLENOID, LOW);

  pinMode(PIN_LED_GREEN, OUTPUT);
  digitalWrite(PIN_LED_GREEN, HIGH);

  pinMode(PIN_LED_RED, OUTPUT);
  digitalWrite(PIN_LED_RED, HIGH);

  deviceMac = WiFi.macAddress();
  emergencyRescuePin = calculateRescuePin(deviceMac);
  Serial.printf("[HARDWARE] MAC: %s | Chave Resgate: %s\n", deviceMac.c_str(), emergencyRescuePin.c_str());

  prefs.begin("cofre", false);
  solenoidPulseMs = prefs.getUShort("pulse", 800);
  portalTimeoutS  = prefs.getUShort("timeout", 180);
  userCount       = prefs.getInt("u_cnt", 0);

  if (userCount == 0) {
    users[0] = {"Fernando (Mestre)", "123456", true};
    userCount = 1;
    prefs.putInt("u_cnt", userCount);
    prefs.putString("u_name_0", users[0].name);
    prefs.putString("u_pin_0", users[0].pin);
    addLog("Sistema", "Sistema", "Inicialização com PIN temporário 123456", true);
  } else {
    for (int i = 0; i < userCount; i++) {
      users[i].name = prefs.getString(("u_name_" + String(i)).c_str(), "Usuario");
      users[i].pin  = prefs.getString(("u_pin_" + String(i)).c_str(), "1234");
      users[i].isAdmin = (i == 0);
    }
  }

  // MIGRACAO: Se houver usuario adicional cadastrado e a senha mestre ainda for 123456,
  // promove o usuario para Mestre e apaga a senha temporaria 123456 para sempre!
  if (userCount > 1 && users[0].pin == "123456") {
    users[0].name = users[1].name;
    users[0].pin  = users[1].pin;
    users[0].isAdmin = true;
    for (int i = 1; i < userCount - 1; i++) {
      users[i] = users[i + 1];
    }
    userCount--;
    prefs.putInt("u_cnt", userCount);
    prefs.putString("u_name_0", users[0].name);
    prefs.putString("u_pin_0", users[0].pin);
    Serial.printf("[MIGRACAO] Senha 123456 ELIMINADA! Mestre atualizado para: %s (PIN: %s)\n", users[0].name.c_str(), users[0].pin.c_str());
  }

  for (int i = 0; i < userCount; i++) {
    Serial.printf("[USUARIO %d] %s | PIN: %s | Admin: %d\n", i, users[i].name.c_str(), users[i].pin.c_str(), users[i].isAdmin);
  }

  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_LED_GREEN, LOW);
    digitalWrite(PIN_LED_RED, HIGH);
    delay(100);
    digitalWrite(PIN_LED_GREEN, HIGH);
    digitalWrite(PIN_LED_RED, LOW);
    delay(100);
  }
  digitalWrite(PIN_LED_GREEN, HIGH);
  digitalWrite(PIN_LED_RED, HIGH);

  soundBuzzer(2000, 100);
  delay(80);
  soundBuzzer(2500, 150);

  startWiFiPortal();
  lastActivityTime = millis();
}

// ============================================================================
// LOOP PRINCIPAL
// ============================================================================
void loop() {
  updateSolenoid();

  if (wifiActive) {
    dnsServer.processNextRequest();
    server.handleClient();
  }

  if (appState == MODE_BLOCKED) {
    digitalWrite(PIN_LED_RED, (millis() / 500) % 2 == 0 ? LOW : HIGH);
    if (millis() >= blockedUntil) {
      digitalWrite(PIN_LED_RED, HIGH);
      appState = MODE_NORMAL;
      failedAttempts = 0;
    }
    delay(30);
    return;
  }

  char key = keypad.getKey();
  if (key) {
    processKey(key);
  }

  static unsigned long lastHeartbeat = 0;
  if (!solenoidActive && (millis() - lastHeartbeat > 1500)) {
    lastHeartbeat = millis();
    digitalWrite(PIN_LED_GREEN, LOW);
    delay(25);
    digitalWrite(PIN_LED_GREEN, HIGH);
  }

  delay(10);
}
