/**
 * @file main.cpp
 * @brief Firmware de Controle para Retrofit de Cofre Eletrônico Inteligente
 * @author Fernando Polito
 * @mcu ESP32-C3FH4 (Pro Mini / Super Mini)
 * @features Teclado Matricial 3x4, Deep Sleep RTC Wake-up, Captive Portal,
 *           LEDs Verde/Vermelho, Buzzer PNP compartilhado, Múltiplos Usuários,
 *           Log Auditado e Chave Mestre de Resgate baseada no MAC físico do ESP32
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <Keypad.h>
#include "web_page.h"

// ============================================================================
// MAPEAMENTO DE PINAGEM FÍSICA (Baseado na Engenharia Reversa da Placa do Cofre)
// ============================================================================
// Solenoide da Trava (Aciona Par Darlington Q1/Q2 via R2 500Ω - Pino 10 do CI antigo)
const uint8_t PIN_SOLENOID   = 7;

// LEDs de Sinalização (Active-LOW: nível LOW acende)
const uint8_t PIN_LED_GREEN  = 8;   // LED Verde (VD) - Pino 12 do CI antigo
const uint8_t PIN_LED_RED    = 10;  // LED Vermelho (VM) - Pino 11 do CI antigo

// Matriz do Teclado Físico (3 Linhas x 4 Colunas = 12 Teclas)
const byte ROWS = 3;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'0', '1', '2', '3'},       // Linha 1: Pino 2 do CI antigo (compartilhado c/ Buzzer Q3)
  {'C', '4', '5', '6'},       // Linha 2: Pino 3 do CI antigo (C = CLEAR)
  {'P', '7', '8', '9'}        // Linha 3: Pino 9 do CI antigo (P = PROGRAM / ENTER)
};

// Linhas da Matriz (Saídas de Varredura no ESP32-C3)
// Linha 1 = GPIO 4 (Pino 2 DIP-16)
// Linha 2 = GPIO 5 (Pino 3 DIP-16)
// Linha 3 = GPIO 6 (Pino 9 DIP-16)
byte rowPins[ROWS] = {4, 5, 6};

// Colunas da Matriz (Entradas com Pull-up interno e RTC Wakeup no ESP32-C3)
// Coluna 1 = GPIO 0 (Pino 5 DIP-16)
// Coluna 2 = GPIO 1 (Pino 6 DIP-16)
// Coluna 3 = GPIO 2 (Pino 7 DIP-16)
// Coluna 4 = GPIO 3 (Pino 8 DIP-16)
byte colPins[COLS] = {0, 1, 2, 3};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ============================================================================
// GESTÃO DE USUÁRIOS E CHAVE DE RESGATE MAC (Zero-Knowledge)
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

// Algoritmo de Derivação da Chave de Resgate por MAC
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

enum AppState { MODE_KEYPAD_ACTIVE, MODE_WIFI_PORTAL, MODE_BLOCKED };
AppState appState = MODE_KEYPAD_ACTIVE;

String inputPinBuffer = "";
uint8_t failedAttempts = 0;
unsigned long blockedUntil = 0;
unsigned long lastActivityTime = 0;
unsigned long solenoidOffTime = 0;
bool solenoidActive = false;

// ============================================================================
// CONTROLE DO BUZZER E LEDS
// ============================================================================
// O buzzer é acionado pelo transistor PNP Q3 ligado à Linha 1 (GPIO 4)
void beep(uint16_t freq, uint16_t durationMs) {
  unsigned long periodUs = 1000000UL / freq;
  unsigned long halfPeriodUs = periodUs / 2;
  unsigned long cycles = ((unsigned long)freq * durationMs) / 1000UL;

  pinMode(rowPins[0], OUTPUT);
  for (unsigned long i = 0; i < cycles; i++) {
    digitalWrite(rowPins[0], LOW);  // Liga PNP Q3 (puxa base para LOW)
    delayMicroseconds(halfPeriodUs);
    digitalWrite(rowPins[0], HIGH); // Desliga PNP Q3
    delayMicroseconds(halfPeriodUs);
  }
  digitalWrite(rowPins[0], HIGH);
}

void beepKey() {
  digitalWrite(PIN_LED_GREEN, LOW);
  beep(2400, 35);
  digitalWrite(PIN_LED_GREEN, HIGH);
}

void beepSuccess() {
  digitalWrite(PIN_LED_GREEN, LOW);
  beep(2000, 80);
  delay(40);
  beep(2800, 120);
  digitalWrite(PIN_LED_GREEN, HIGH);
}

void beepRescue() {
  digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_RED, LOW);
  beep(1800, 150);
  delay(50);
  beep(2400, 150);
  delay(50);
  beep(3200, 250);
  digitalWrite(PIN_LED_GREEN, HIGH);
  digitalWrite(PIN_LED_RED, HIGH);
}

void beepError() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_LED_RED, LOW);
    beep(800, 120);
    digitalWrite(PIN_LED_RED, HIGH);
    delay(80);
  }
}

// ============================================================================
// ACIONAMENTO DA BOBINA / SOLENOIDE
// ============================================================================
void triggerSolenoid(String authorizedUser, String method, bool isRescue = false) {
  Serial.printf("[COFRE] Solenoide ATIVADO para %s via %s!\n", authorizedUser.c_str(), method.c_str());
  digitalWrite(PIN_SOLENOID, HIGH);
  digitalWrite(PIN_LED_GREEN, LOW);
  solenoidActive = true;
  solenoidOffTime = millis() + solenoidPulseMs;

  addLog(authorizedUser, method, isRescue ? "🚨 DESTRAVAMENTO DE RESGATE (CHAVE MAC)" : "Abertura autorizada", true);

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
// DEEP SLEEP COM WAKEUP RTC POR QUALQUER TECLA
// ============================================================================
void enterDeepSleep() {
  Serial.println("[ENERGIA] Entrando em Deep Sleep (~15uA)...");
  digitalWrite(PIN_SOLENOID, LOW);
  digitalWrite(PIN_LED_GREEN, HIGH); // Apaga LED (Active-LOW)
  digitalWrite(PIN_LED_RED, HIGH);   // Apaga LED (Active-LOW)

  // As 3 linhas são aterradas para criar referência LOW
  for (int r = 0; r < ROWS; r++) {
    pinMode(rowPins[r], OUTPUT);
    digitalWrite(rowPins[r], LOW);
  }

  // As 4 colunas (GPIOs 0, 1, 2, 3) ficam em Pull-Up e ativam o Wake-up no LOW
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
// ROTAS DO CAPTIVE PORTAL (RESGATE E CONFIGURAÇÕES)
// ============================================================================
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleApiStatus() {
  lastActivityTime = millis();
  String json = "{";
  json += "\"status\":\"ok\",";
  json += "\"pulseMs\":" + String(solenoidPulseMs) + ",";
  json += "\"sleepTimeout\":" + String(portalTimeoutS) + ",";
  json += "\"batteryV\":9.0,";
  json += "\"deviceMac\":\"" + deviceMac + "\",";

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

  // 1. Testa se foi usada a Chave de Resgate MAC
  if (pin == emergencyRescuePin) {
    triggerSolenoid("RESCUE (Chave MAC)", "Web Portal", true);
    server.send(200, "application/json", "{\"status\":\"ok\",\"isRescue\":true,\"userName\":\"Chave de Emergência MAC\"}");
    return;
  }

  // 2. Testa usuários normais
  for (int i = 0; i < userCount; i++) {
    if (users[i].pin == pin) {
      triggerSolenoid(users[i].name, "Web Portal", false);
      server.send(200, "application/json", "{\"status\":\"ok\",\"isRescue\":false,\"userName\":\"" + users[i].name + "\"}");
      return;
    }
  }

  addLog("Desconhecido", "Web Portal", "Tentativa de senha incorreta", false);
  server.send(401, "application/json", "{\"status\":\"error\",\"message\":\"Senha incorreta! Acesso negado.\"}");
  beepError();
}

void handleApiEmergencyReset() {
  lastActivityTime = millis();
  String rescuePin = server.arg("rescuePin");

  if (rescuePin == emergencyRescuePin) {
    users[0].pin = "123456";
    prefs.putString("u_pin_0", "123456");
    addLog("RESCUE (Chave MAC)", "Web Portal", "Senha Mestre restaurada para 123456", true);
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Senha Mestre restaurada para 123456 com sucesso!\"}");
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

  if (adminPin != users[0].pin && adminPin != emergencyRescuePin) {
    server.send(403, "application/json", "{\"status\":\"error\",\"message\":\"Senha Mestre incorreta!\"}");
    return;
  }

  if (userCount >= MAX_USERS) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Limite de usuários atingido!\"}");
    return;
  }

  users[userCount] = {name, pin, false};
  userCount++;
  prefs.putInt("u_cnt", userCount);
  prefs.putString(("u_name_" + String(userCount - 1)).c_str(), name);
  prefs.putString(("u_pin_" + String(userCount - 1)).c_str(), pin);

  addLog("Admin", "Web Portal", "Novo usuário cadastrado: " + name, true);
  server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Usuário cadastrado com sucesso!\"}");
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
  server.on("/api/emergency_reset", HTTP_POST, handleApiEmergencyReset);
  server.on("/api/add_user", HTTP_POST, handleApiAddUser);
  server.on("/api/config", HTTP_POST, handleApiConfig);
  server.on("/api/sleep", HTTP_POST, handleApiSleep);
  server.onNotFound(handleRoot);

  server.begin();
  beepSuccess();
}

// ============================================================================
// PROCESSAMENTO DO TECLADO FÍSICO (3x4: 0-9, CLEAR, PROGRAM)
// ============================================================================
void processKey(char key) {
  lastActivityTime = millis();
  beepKey();

  // Tecla 'P' (PROGRAM / ENTER)
  if (key == 'P' || key == '#') {
    // Comando para ligar Wi-Fi: "C000" ou "*000" seguido de P/ENTER
    if (inputPinBuffer == "C000" || inputPinBuffer == "*000" || inputPinBuffer == "000") {
      startWiFiCaptivePortal();
      inputPinBuffer = "";
      return;
    }

    // 1. Testa se digitou a Chave de Resgate MAC
    if (inputPinBuffer == emergencyRescuePin) {
      failedAttempts = 0;
      triggerSolenoid("RESCUE (Chave MAC)", "Teclado", true);
      inputPinBuffer = "";
      return;
    }

    // 2. Testa usuários normais cadastrados
    bool authenticated = false;
    for (int i = 0; i < userCount; i++) {
      if (inputPinBuffer == users[i].pin) {
        authenticated = true;
        failedAttempts = 0;
        triggerSolenoid(users[i].name, "Teclado", false);
        break;
      }
    }

    if (!authenticated) {
      Serial.println("[AUTH] Senha incorreta no teclado!");
      failedAttempts++;
      addLog("Desconhecido", "Teclado", "Tentativa de senha inválida: " + inputPinBuffer, false);
      beepError();

      if (failedAttempts >= 5) {
        appState = MODE_BLOCKED;
        blockedUntil = millis() + 300000; // Bloqueio de 5 minutos
      } else if (failedAttempts >= 3) {
        appState = MODE_BLOCKED;
        blockedUntil = millis() + 60000;  // Bloqueio de 1 minuto
      }
    }

    inputPinBuffer = "";
  }
  // Tecla 'C' (CLEAR / LIMPAR BUFFER)
  else if (key == 'C' || key == '*') {
    inputPinBuffer = "";
    digitalWrite(PIN_LED_RED, LOW);
    beep(1200, 100);
    digitalWrite(PIN_LED_RED, HIGH);
  }
  // Teclas Numéricas (0 a 9)
  else {
    if (inputPinBuffer.length() < 8) {
      inputPinBuffer += key;
    }
  }
}

// ============================================================================
// INICIALIZAÇÃO (SETUP)
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  // Configuração dos Pinos de Saída
  pinMode(PIN_SOLENOID, OUTPUT);
  digitalWrite(PIN_SOLENOID, LOW);

  pinMode(PIN_LED_GREEN, OUTPUT);
  digitalWrite(PIN_LED_GREEN, HIGH); // Apagado (Active-LOW)

  pinMode(PIN_LED_RED, OUTPUT);
  digitalWrite(PIN_LED_RED, HIGH);   // Apagado (Active-LOW)

  // Lê o endereço físico MAC do ESP32 e calcula a Chave Mestre de Resgate
  deviceMac = WiFi.macAddress();
  emergencyRescuePin = calculateRescuePin(deviceMac);
  Serial.printf("[HARDWARE] MAC do ESP32: %s | Chave Resgate: %s\n", deviceMac.c_str(), emergencyRescuePin.c_str());

  // Memória Não Volátil (Flash Preferences)
  prefs.begin("cofre", false);
  solenoidPulseMs = prefs.getUShort("pulse", 800);
  userCount = prefs.getInt("u_cnt", 0);

  // Primeira inicialização de fábrica
  if (userCount == 0) {
    users[0] = {"Fernando (Mestre)", "123456", true};
    userCount = 1;
    prefs.putInt("u_cnt", userCount);
    prefs.putString("u_name_0", users[0].name);
    prefs.putString("u_pin_0", users[0].pin);
    addLog("Sistema", "Sistema", "Inicialização de Fábrica (Chave MAC Ativa)", true);
  } else {
    for (int i = 0; i < userCount; i++) {
      users[i].name = prefs.getString(("u_name_" + String(i)).c_str(), "Usuario");
      users[i].pin  = prefs.getString(("u_pin_" + String(i)).c_str(), "1234");
      users[i].isAdmin = (i == 0);
    }
  }

  Serial.println("[COFRE] Sistema pronto.");
  lastActivityTime = millis();
}

// ============================================================================
// LOOP PRINCIPAL
// ============================================================================
void loop() {
  updateSolenoid();

  // Estado de Bloqueio por tentativas erradas
  if (appState == MODE_BLOCKED) {
    digitalWrite(PIN_LED_RED, (millis() / 500) % 2 == 0 ? LOW : HIGH); // Pisca LED vermelho
    if (millis() >= blockedUntil) {
      digitalWrite(PIN_LED_RED, HIGH);
      appState = MODE_KEYPAD_ACTIVE;
      failedAttempts = 0;
    }
    delay(50);
    return;
  }

  // Estado de Portal Wi-Fi Ativo
  if (appState == MODE_WIFI_PORTAL) {
    digitalWrite(PIN_LED_GREEN, (millis() / 500) % 2 == 0 ? LOW : HIGH); // Pisca LED verde
    dnsServer.processNextRequest();
    server.handleClient();
    if (millis() - lastActivityTime > (portalTimeoutS * 1000UL)) {
      digitalWrite(PIN_LED_GREEN, HIGH);
      enterDeepSleep();
    }
    return;
  }

  // Estado Normal de Teclado
  char key = keypad.getKey();
  if (key) {
    processKey(key);
  }

  // Timeout de inatividade do teclado -> Deep Sleep (~15uA)
  if (!solenoidActive && (millis() - lastActivityTime > 8000)) {
    enterDeepSleep();
  }

  delay(20);
}
