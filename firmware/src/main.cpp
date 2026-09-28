/**
 * @file main.cpp
 * @brief Firmware de Controle para Retrofit de Cofre Eletrônico Inteligente
 * @author Fernando Polito
 * @mcu ESP32-C3FH4 (Pro Mini / Super Mini)
 * @features Teclado Matricial, Deep Sleep RTC, Captive Portal, Autenticação de Múltiplos Usuários e Log Auditado
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <Keypad.h>
#include "web_page.h"

// ============================================================================
// MAPEAMENTO DE PINAGEM (ESP32-C3)
// ============================================================================
const uint8_t PIN_SOLENOID  = 7;  // Gate do MOSFET N-Channel da Bobina
const uint8_t PIN_BUZZER    = 8;  // Buzzer piezoelétrico
const uint8_t PIN_LED       = 9;  // LED de status (ativo em LOW no SuperMini)
const uint8_t PIN_RESET_BTN = 10; // Botão de reset físico interno na porta

// Matriz do Teclado Numérico (4 Linhas x 3 Colunas)
const byte ROWS = 4;
const byte COLS = 3;

char keys[ROWS][COLS] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'},
  {'*', '0', '#'}
};

// Linhas nos pinos RTC GPIO 0 a 3 (permitem despertar do Deep Sleep)
byte rowPins[ROWS] = {0, 1, 2, 3};
byte colPins[COLS] = {4, 5, 6};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ============================================================================
// ESTRUTURA DE USUÁRIOS E AUDITORIA
// ============================================================================
struct SafeUser {
  String name;
  String pin;
  bool isAdmin;
};

const int MAX_USERS = 5;
SafeUser users[MAX_USERS];
int userCount = 0;

struct AuditLog {
  String user;
  String method;
  String action;
  bool success;
  unsigned long timeSec;
};

const int MAX_LOGS = 10;
AuditLog logs[MAX_LOGS];
int logCount = 0;

void addLog(String user, String method, String action, bool success) {
  if (logCount < MAX_LOGS) {
    logs[logCount] = {user, method, action, success, millis() / 1000};
    logCount++;
  } else {
    // Desloca logs para abrir espaço para o mais recente
    for (int i = 0; i < MAX_LOGS - 1; i++) {
      logs[i] = logs[i + 1];
    }
    logs[MAX_LOGS - 1] = {user, method, action, success, millis() / 1000};
  }
}

// ============================================================================
// VARIÁVEIS GLOBAIS E NVS
// ============================================================================
Preferences prefs;
DNSServer dnsServer;
WebServer server(80);

uint16_t solenoidPulseMs = 800;
uint16_t portalTimeoutS  = 180;

enum AppState {
  MODE_KEYPAD_ACTIVE,
  MODE_WIFI_PORTAL,
  MODE_BLOCKED
};

AppState appState = MODE_KEYPAD_ACTIVE;
String inputPinBuffer = "";
uint8_t failedAttempts = 0;
unsigned long blockedUntil = 0;
unsigned long lastActivityTime = 0;
unsigned long solenoidOffTime = 0;
bool solenoidActive = false;

// ============================================================================
// FUNÇÕES AUXILIARES DE SOM E LED
// ============================================================================
void beep(uint16_t freq, uint16_t durationMs) {
  tone(PIN_BUZZER, freq, durationMs);
  digitalWrite(PIN_LED, LOW);
  delay(durationMs);
  digitalWrite(PIN_LED, HIGH);
}

void beepKey()     { beep(2400, 40); }
void beepSuccess() { beep(2000, 80); delay(50); beep(2800, 120); }
void beepError()   { for (int i = 0; i < 3; i++) { beep(800, 120); delay(80); } }

// ============================================================================
// ACIONAMENTO DA BOBINA (SOLENOIDE)
// ============================================================================
void triggerSolenoid(String authorizedUser, String method) {
  Serial.printf("[COFRE] Abertura autorizada para %s via %s!\n", authorizedUser.c_str(), method.c_str());
  digitalWrite(PIN_SOLENOID, HIGH);
  solenoidActive = true;
  solenoidOffTime = millis() + solenoidPulseMs;
  addLog(authorizedUser, method, "Abertura autorizada", true);
  beepSuccess();
}

void updateSolenoid() {
  if (solenoidActive && millis() >= solenoidOffTime) {
    digitalWrite(PIN_SOLENOID, LOW);
    solenoidActive = false;
  }
}

// ============================================================================
// GERENCIAMENTO DE ENERGIA (DEEP SLEEP)
// ============================================================================
void enterDeepSleep() {
  Serial.println("[ENERGIA] Entrando em Deep Sleep (~15uA)...");
  digitalWrite(PIN_SOLENOID, LOW);
  digitalWrite(PIN_LED, HIGH);

  for (int c = 0; c < COLS; c++) {
    pinMode(colPins[c], OUTPUT);
    digitalWrite(colPins[c], LOW);
  }

  for (int r = 0; r < ROWS; r++) {
    pinMode(rowPins[r], INPUT_PULLUP);
    esp_deep_sleep_enable_gpio_wakeup(1ULL << rowPins[r], ESP_GPIO_WAKEUP_GPIO_LOW);
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.flush();
  esp_deep_sleep_start();
}

// ============================================================================
// ROTAS DO CAPTIVE PORTAL COM AUTENTICAÇÃO
// ============================================================================
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleApiStatus() {
  lastActivityTime = millis();
  String json = "{";
  json += "\"status\":\"ok\",";
  json += "\"pulseMs\":" + String(solenoidPulseMs) + ",";
  json += "\"batteryV\":8.8,";
  
  // Lista de usuários (sem expor PINs)
  json += "\"users\":[";
  for (int i = 0; i < userCount; i++) {
    json += "{\"id\":" + String(i + 1) + ",";
    json += "\"name\":\"" + users[i].name + "\",";
    json += "\"role\":\"" + String(users[i].isAdmin ? "admin" : "user") + "\"}";
    if (i < userCount - 1) json += ",";
  }
  json += "],";

  // Logs recentes
  json += "\"logs\":[";
  for (int i = logCount - 1; i >= 0; i--) {
    json += "{\"user\":\"" + logs[i].user + "\",";
    json += "\"method\":\"" + logs[i].method + "\",";
    json += "\"action\":\"" + logs[i].action + "\",";
    json += "\"time\":\"Há " + String((millis() / 1000) - logs[i].timeSec) + "s\",";
    json += "\"success\":" + String(logs[i].success ? "true" : "false") + "}";
    if (i > 0) json += ",";
  }
  json += "]}";

  server.send(200, "application/json", json);
}

void handleApiUnlock() {
  lastActivityTime = millis();
  String pin = server.arg("pin");

  if (pin.length() == 0) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Senha não informada\"}");
    return;
  }

  // Procura usuário com este PIN
  for (int i = 0; i < userCount; i++) {
    if (users[i].pin == pin) {
      triggerSolenoid(users[i].name, "Web Portal");
      server.send(200, "application/json", "{\"status\":\"ok\",\"userName\":\"" + users[i].name + "\"}");
      return;
    }
  }

  // Falha na senha
  addLog("Desconhecido", "Web Portal", "Tentativa de senha incorreta", false);
  server.send(401, "application/json", "{\"status\":\"error\",\"message\":\"Senha incorreta! Acesso negado.\"}");
  beepError();
}

void handleApiAddUser() {
  lastActivityTime = millis();
  String name     = server.arg("name");
  String pin      = server.arg("pin");
  String adminPin = server.arg("adminPin");

  // Valida admin
  if (adminPin != users[0].pin) {
    server.send(403, "application/json", "{\"status\":\"error\",\"message\":\"Senha Mestre incorreta!\"}");
    return;
  }

  if (userCount >= MAX_USERS) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Limite máximo de usuários atingido!\"}");
    return;
  }

  users[userCount] = {name, pin, false};
  userCount++;
  
  // Persiste na NVS
  prefs.putInt("u_cnt", userCount);
  prefs.putString(("u_name_" + String(userCount - 1)).c_str(), name);
  prefs.putString(("u_pin_" + String(userCount - 1)).c_str(), pin);

  addLog("Admin", "Web Portal", "Novo usuário cadastrado: " + name, true);
  server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Usuário cadastrado!\"}");
}

void handleApiConfig() {
  lastActivityTime = millis();
  if (server.hasArg("pulse")) {
    solenoidPulseMs = server.arg("pulse").toInt();
    prefs.putUShort("pulse", solenoidPulseMs);
  }
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleApiSleep() {
  server.send(200, "application/json", "{\"status\":\"ok\"}");
  delay(500);
  enterDeepSleep();
}

void startWiFiCaptivePortal() {
  Serial.println("[WIFI] Ativando Captive Portal...");
  appState = MODE_WIFI_PORTAL;
  lastActivityTime = millis();

  WiFi.mode(WIFI_AP);
  WiFi.softAP("Cofre-Smart-Setup");

  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  dnsServer.start(53, "*", apIP);

  server.on("/", handleRoot);
  server.on("/api/status", handleApiStatus);
  server.on("/api/unlock", HTTP_POST, handleApiUnlock);
  server.on("/api/add_user", HTTP_POST, handleApiAddUser);
  server.on("/api/config", HTTP_POST, handleApiConfig);
  server.on("/api/sleep", HTTP_POST, handleApiSleep);
  server.onNotFound(handleRoot);

  server.begin();
  beepSuccess();
}

// ============================================================================
// LEITURA DO TECLADO FÍSICO COM IDENTIFICAÇÃO DE USUÁRIO
// ============================================================================
void processKey(char key) {
  lastActivityTime = millis();
  beepKey();

  if (key == '#') {
    // Código especial para Wi-Fi
    if (inputPinBuffer == "*000") {
      startWiFiCaptivePortal();
      inputPinBuffer = "";
      return;
    }

    // Valida o PIN digitado contra todos os usuários cadastrados
    bool authenticated = false;
    for (int i = 0; i < userCount; i++) {
      if (inputPinBuffer == users[i].pin) {
        authenticated = true;
        failedAttempts = 0;
        triggerSolenoid(users[i].name, "Teclado");
        break;
      }
    }

    if (!authenticated) {
      Serial.println("[AUTH] Senha incorreta no teclado!");
      failedAttempts++;
      addLog("Desconhecido", "Teclado", "Tentativa de senha inválida", false);
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
  } else if (key == '*') {
    inputPinBuffer = "";
    beep(1200, 150);
  } else {
    if (inputPinBuffer.length() < 8) {
      inputPinBuffer += key;
    }
  }
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(PIN_SOLENOID, OUTPUT);
  digitalWrite(PIN_SOLENOID, LOW);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);
  pinMode(PIN_RESET_BTN, INPUT_PULLUP);

  prefs.begin("cofre", false);
  solenoidPulseMs = prefs.getUShort("pulse", 800);
  userCount = prefs.getInt("u_cnt", 0);

  // Carrega ou inicializa usuário Mestre
  if (userCount == 0 || digitalRead(PIN_RESET_BTN) == LOW) {
    users[0] = {"Fernando (Mestre)", "123456", true};
    users[1] = {"Familia / Reserva", "2580", false};
    userCount = 2;
    prefs.putInt("u_cnt", userCount);
    prefs.putString("u_name_0", users[0].name);
    prefs.putString("u_pin_0", users[0].pin);
    prefs.putString("u_name_1", users[1].name);
    prefs.putString("u_pin_1", users[1].pin);
    addLog("Sistema", "Sistema", "Inicializacao de Fabrica", true);
  } else {
    for (int i = 0; i < userCount; i++) {
      users[i].name = prefs.getString(("u_name_" + String(i)).c_str(), "Usuario");
      users[i].pin  = prefs.getString(("u_pin_" + String(i)).c_str(), "1234");
      users[i].isAdmin = (i == 0);
    }
  }

  Serial.println("[COFRE] Pronto.");
  lastActivityTime = millis();
}

void loop() {
  updateSolenoid();

  if (appState == MODE_BLOCKED) {
    if (millis() >= blockedUntil) {
      appState = MODE_KEYPAD_ACTIVE;
      failedAttempts = 0;
    }
    delay(50);
    return;
  }

  if (appState == MODE_WIFI_PORTAL) {
    dnsServer.processNextRequest();
    server.handleClient();
    if (millis() - lastActivityTime > (portalTimeoutS * 1000UL)) {
      enterDeepSleep();
    }
    return;
  }

  char key = keypad.getKey();
  if (key) {
    processKey(key);
  }

  if (!solenoidActive && (millis() - lastActivityTime > 8000)) {
    enterDeepSleep();
  }

  delay(20);
}
