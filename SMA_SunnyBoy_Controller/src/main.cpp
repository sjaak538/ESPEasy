// ESP32 WiFi controller for an SMA Sunny Boy inverter over Modbus TCP.
//
// - Potentiometer on POT_PIN sets a 0-100% active power limit setpoint.
// - SSD1306 OLED shows WiFi/Modbus status, measured AC power and the
//   setpoint.
// - The setpoint is written to REG_POWER_LIMIT only when it changes by
//   more than POT_DEADBAND_PERCENT and WRITE_ENABLED is set in config.h.
//
// See config.example.h for the settings to copy into config.h.

#include <WiFi.h>
#include <ArduinoModbus.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "config.h"

static const unsigned long READ_INTERVAL_MS       = 5000;
static const unsigned long WRITE_MIN_INTERVAL_MS   = 2000;
static const unsigned long WIFI_RETRY_INTERVAL_MS  = 5000;
static const uint8_t       POT_DEADBAND_PERCENT    = 2;
static const uint8_t       POT_SAMPLES             = 16; // averaged per reading

Adafruit_SSD1306 display(128, 64, &Wire, -1);
WiFiClient wifiClient;
ModbusTCPClient modbus(wifiClient);

unsigned long lastReadMs  = 0;
unsigned long lastWriteMs = 0;
unsigned long lastWifiTryMs = 0;

bool    modbusConnected = false;
int32_t lastAcPowerW    = 0;
bool    haveAcPower      = false;
uint8_t lastSentPercent  = 255; // 255 = never sent yet

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }
  if (millis() - lastWifiTryMs < WIFI_RETRY_INTERVAL_MS) {
    return;
  }
  lastWifiTryMs = millis();
  Serial.println(F("WiFi: connecting..."));
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

bool ensureModbus() {
  if (WiFi.status() != WL_CONNECTED) {
    modbusConnected = false;
    return false;
  }
  if (modbusConnected && wifiClient.connected()) {
    return true;
  }
  Serial.println(F("Modbus: connecting to inverter..."));
  modbusConnected = modbus.begin(SMA_IP, SMA_PORT);
  if (!modbusConnected) {
    Serial.println(F("Modbus: connect failed"));
  }
  return modbusConnected;
}

// Reads an S32 (2 registers, big-endian) input register.
bool readS32InputRegister(int address, int32_t &result) {
  if (!modbus.requestFrom(SMA_UNIT_ID, INPUT_REGISTERS, address, 2)) {
    Serial.print(F("Modbus: read failed @"));
    Serial.println(address);
    modbusConnected = false;
    return false;
  }
  uint32_t hi = (uint16_t)modbus.read();
  uint32_t lo = (uint16_t)modbus.read();
  result = (int32_t)((hi << 16) | lo);
  return true;
}

bool writeS32HoldingRegister(int address, int32_t value) {
  if (!modbus.beginTransmission(SMA_UNIT_ID, HOLDING_REGISTERS, address, 2)) {
    modbusConnected = false;
    return false;
  }
  modbus.write((uint16_t)((uint32_t)value >> 16));
  modbus.write((uint16_t)((uint32_t)value & 0xFFFF));
  if (!modbus.endTransmission()) {
    Serial.print(F("Modbus: write failed @"));
    Serial.println(address);
    modbusConnected = false;
    return false;
  }
  return true;
}

// Averages a batch of ADC samples and maps 0-4095 to 0-100%.
uint8_t readPotPercent() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < POT_SAMPLES; i++) {
    sum += analogRead(POT_PIN);
    delay(1);
  }
  uint32_t avg = sum / POT_SAMPLES;
  return (uint8_t)constrain((avg * 100UL) / 4095UL, 0UL, 100UL);
}

void updateDisplay(uint8_t potPercent) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.print(F("WiFi: "));
  display.println(WiFi.status() == WL_CONNECTED ? F("OK") : F("..."));

  display.setCursor(0, 10);
  display.print(F("Modbus: "));
  display.println(modbusConnected ? F("OK") : F("..."));

  display.setCursor(0, 24);
  display.print(F("AC power: "));
  if (haveAcPower) {
    display.print(lastAcPowerW);
    display.println(F(" W"));
  } else {
    display.println(F("--"));
  }

  display.setCursor(0, 38);
  display.print(F("Limit set: "));
  display.print(potPercent);
  display.println(F(" %"));

  display.setTextSize(2);
  display.setCursor(0, 48);
  display.print(potPercent);
  display.println(F("%"));

#if !WRITE_ENABLED
  display.setTextSize(1);
  display.setCursor(70, 48);
  display.print(F("(read-only)"));
#endif

  display.display();
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("OLED: init failed"));
  }
  display.clearDisplay();
  display.display();

  analogReadResolution(12);
  connectWiFi();
}

void loop() {
  connectWiFi();

  uint8_t potPercent = readPotPercent();

  if (ensureModbus()) {
    unsigned long now = millis();

    if (now - lastReadMs >= READ_INTERVAL_MS) {
      lastReadMs = now;
      haveAcPower = readS32InputRegister(REG_AC_POWER, lastAcPowerW);
    }

#if WRITE_ENABLED
    bool changedEnough =
      lastSentPercent == 255 ||
      (uint8_t)abs((int)potPercent - (int)lastSentPercent) >= POT_DEADBAND_PERCENT;

    if (changedEnough && millis() - lastWriteMs >= WRITE_MIN_INTERVAL_MS) {
      lastWriteMs = millis();
      if (writeS32HoldingRegister(REG_POWER_LIMIT, (int32_t)potPercent)) {
        lastSentPercent = potPercent;
        Serial.print(F("Modbus: power limit set to "));
        Serial.print(potPercent);
        Serial.println(F("%"));
      }
    }
#endif
  }

  updateDisplay(potPercent);
  delay(200);
}
