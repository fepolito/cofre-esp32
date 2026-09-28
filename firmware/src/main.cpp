/**
 * @file main.cpp
 * @brief Firmware de Controle para Retrofit de Cofre Eletrônico Inteligente
 * @author Fernando Polito
 * @mcu ESP32-C3FH4 (Pro Mini / Super Mini)
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
// Driver de Potência e Atuadores
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
// VARIÁVEIS GLOBAIS E CONFIGURAÇÕES PERSISTENTES (NVS)
// ============================================================================
Preferences prefs;
DNSServer dnsServer;
WebServer server(80);

String currentPassword   = "123456";  // Senha padrão de fábrica
uint16_t solenoidPulseMs = 800;       // Duração padrão do pulso mecânico (ms)
uint16_t portalTimeoutS  = 180;       // Timeout do Wi-Fi Captive Portal (segundos)

// Estado da Aplicação
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
// FUNÇÕES AUXILIARES DE HARDWARE (SOM E LED)
// ============================================================================
void beep(uint16_t freq, uint16_t durationMs) {
  tone(PIN_BUZZER, freq, durationMs);
  digitalWrite(PIN_LED, LOW); // Acende LED
  delay(durationMs);
  digitalWrite(PIN_LED, HIGH); // Apaga LED
}

void beepKey() {
  beep(2400, 40);
}

void beepSuccess() {
  beep(2000, 80);
  delay(50);
  beep(2800, 120);
}

void beepError() {
  for (int i = 0; i < 3; i++) {
    beep(800, 120);
    delay(80);
  }
}

// ============================================================================
// ACIONAMENTO SEGURO DA BOBINA (SOLENOIDE)
// ============================================================================
void triggerSolenoid() {
  Serial.println("[COFRE] Solenoide ATIVADO!");
  digitalWrite(PIN_SOLENOID, HIGH);
  solenoidActive = true;
  solenoidOffTime = millis() + solenoidPulseMs;
  beepSuccess();
}

void updateSolenoid() {
  if (solenoidActive && millis() >= solenoidOffTime) {
    digitalWrite(PIN_SOLENOID, LOW);
    solenoidActive = false;
    Serial.println("[COFRE] Solenoide DESLIGADO.");
  }
}

// ============================================================================
// GERENCIAMENTO DE ENERGIA (DEEP SLEEP COM WAKEUP POR MATRIZ)
// ============================================================================
void enterDeepSleep() {
  Serial.println("[ENERGIA] Preparando entrada em Deep Sleep...");
  digitalWrite(PIN_SOLENOID, LOW);
  digitalWrite(PIN_LED, HIGH);

  // Coloca as colunas em nível LOW para que qualquer tecla aterrada gere nível LOW nas linhas
  for (int c = 0; c < COLS; c++) {
    pinMode(colPins[c], OUTPUT);
    digitalWrite(colPins[c], LOW);
  }

  // Habilita Wake-up por nível LOW em qualquer uma das linhas RTC
  for (int r = 0; r < ROWS; r++) {
    pinMode(rowPins[r], INPUT_PULLUP);
    esp_deep_sleep_enable_gpio_wakeup(1ULL << rowPins[r], ESP_GPIO_WAKEUP_GPIO_LOW);
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  Serial.println("[ENERGIA] Dormindo agora (consumo ~15uA). Zzz...");
  Serial.flush();
  esp_deep_sleep_start();
}

// ============================================================================
// CAPTIVE PORTAL & WEB SERVER
// ============================================================================
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleApiStatus() {
  lastActivityTime = millis();
  String json = "{";
  json += "\"status\":\"ok\",";
  json += "\"pulseMs\":" + String(solenoidPulseMs) + ",";
  json += "\"batteryV\":8.8";
  json += "}";
  server.send(200, "application/json", json);
}

void handleApiUnlock() {
  lastActivityTime = millis();
  triggerSolenoid();
  server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Destravado com sucesso!\"}");
}

void handleApiPassword() {
  lastActivityTime = millis();
  String curr = server.arg("curr");
  String next = server.arg("new");

  if (curr == currentPassword && next.length() >= 4 && next.length() <= 8) {
    currentPassword = next;
    prefs.putString("pwd", currentPassword);
    server.send(200, "application/json", "{\"status\":\"ok\"}");
    beepSuccess();
  } else {
    server.send(403, "application/json", "{\"status\":\"error\",\"message\":\"Senha incorreta\"}");
    beepError();
  }
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
  server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Dormindo...\"}");
  delay(500);
  enterDeepSleep();
}

void startWiFiCaptivePortal() {
  Serial.println("[WIFI] Iniciando SoftAP e Captive Portal...");
  appState = MODE_WIFI_PORTAL;
  lastActivityTime = millis();

  WiFi.mode(WIFI_AP);
  WiFi.softAP("Cofre-Smart-Setup");

  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  // DNS Server redireciona todos os domínios para o IP do ESP32 (Captive Portal)
  dnsServer.start(53, "*", apIP);

  server.on("/", handleRoot);
  server.on("/api/status", handleApiStatus);
  server.on("/api/unlock", HTTP_POST, handleApiUnlock);
  server.on("/api/password", HTTP_POST, handleApiPassword);
  server.on("/api/config", HTTP_POST, handleApiConfig);
  server.on("/api/sleep", HTTP_POST, handleApiSleep);
  server.onNotFound(handleRoot); // Redirecionamento captive para Android / iOS

  server.begin();
  beepSuccess();
  Serial.println("[WIFI] Pronto! Conecte na rede 'Cofre-Smart-Setup' e acesse 192.168.4.1");
}

// ============================================================================
// LÓGICA DO TECLADO E VALIDAÇÃO DE SENHA
// ============================================================================
void processKey(char key) {
  lastActivityTime = millis();
  beepKey();

  if (key == '#') {
    // Tecla de Confirmação (Enter)
    if (inputPinBuffer == currentPassword) {
      Serial.println("[AUTH] Senha correta!");
      failedAttempts = 0;
      triggerSolenoid();
      inputPinBuffer = "";
    } else if (inputPinBuffer == "*000") {
      // Código especial para ligar o Wi-Fi Captive Portal
      startWiFiCaptivePortal();
      inputPinBuffer = "";
    } else {
      Serial.println("[AUTH] Senha incorreta!");
      failedAttempts++;
      beepError();
      inputPinBuffer = "";

      if (failedAttempts >= 5) {
        appState = MODE_BLOCKED;
        blockedUntil = millis() + 300000; // Bloqueio de 5 minutos
        Serial.println("[SEGURANCA] Sistema bloqueado por 5 minutos!");
      } else if (failedAttempts >= 3) {
        appState = MODE_BLOCKED;
        blockedUntil = millis() + 60000;  // Bloqueio de 1 minuto
        Serial.println("[SEGURANCA] Sistema bloqueado por 1 minuto!");
      }
    }
  } else if (key == '*') {
    // Tecla Limpar
    inputPinBuffer = "";
    beep(1200, 150);
  } else {
    // Dígito 0-9
    if (inputPinBuffer.length() < 8) {
      inputPinBuffer += key;
    }
  }
}

// ============================================================================
// SETUP & LOOP PRINCIPAL
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

  // Inicializa NVS e recupera configurações
  prefs.begin("cofre", false);
  currentPassword = prefs.getString("pwd", "123456");
  solenoidPulseMs = prefs.getUShort("pulse", 800);

  // Verifica se o botão de reset físico interno está pressionado
  if (digitalRead(PIN_RESET_BTN) == LOW) {
    Serial.println("[RESET] Restaurando configurações de fábrica...");
    currentPassword = "123456";
    prefs.putString("pwd", currentPassword);
    solenoidPulseMs = 800;
    prefs.putUShort("pulse", solenoidPulseMs);
    beepSuccess();
  }

  Serial.println("[COFRE] Inicializado com sucesso.");
  lastActivityTime = millis();
}

void loop() {
  updateSolenoid();

  // 1. Modo Bloqueado por Força Bruta
  if (appState == MODE_BLOCKED) {
    if (millis() >= blockedUntil) {
      appState = MODE_KEYPAD_ACTIVE;
      failedAttempts = 0;
      beep(1800, 200);
      Serial.println("[SEGURANCA] Bloqueio finalizado.");
    }
    delay(50);
    return;
  }

  // 2. Modo Wi-Fi Captive Portal
  if (appState == MODE_WIFI_PORTAL) {
    dnsServer.processNextRequest();
    server.handleClient();

    // Timeout por inatividade Wi-Fi para não descarregar a bateria
    if (millis() - lastActivityTime > (portalTimeoutS * 1000UL)) {
      Serial.println("[WIFI] Timeout de inatividade. Desligando rádio...");
      enterDeepSleep();
    }
    return;
  }

  // 3. Modo Normal (Teclado Ativo)
  char key = keypad.getKey();
  if (key) {
    processKey(key);
  }

  // Se ficar inativo por mais de 8 segundos no teclado, entra em Deep Sleep
  if (!solenoidActive && (millis() - lastActivityTime > 8000)) {
    enterDeepSleep();
  }

  delay(20);
}
