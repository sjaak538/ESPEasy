// ESP32 WiFi controller for an SMA Sunny Boy inverter over Modbus TCP.
//
// One firmware image for every install - no per-customer recompiling:
// - First boot (or after a WiFi reset) opens a WiFiManager captive
//   portal so the installer/customer connects with a phone, picks
//   their own WiFi network, and fills in the inverter's IP/port/unit
//   ID and Modbus registers right there.
// - Anything not filled in during setup (write-enable, register
//   scale) can still be changed later from a web page at /config.
// - Settings persist in flash (NVS) across reboots and firmware
//   updates.
//
// Hardware: potentiometer on POT_PIN sets a 0-100% active power limit
// setpoint, shown together with live readings on a 20x4 I2C LCD.
//
// Arduino IDE setup: Board = "ESP32 Dev Module" (esp32 core by
// Espressif Systems). Install these libraries via Sketch -> Include
// Library -> Manage Libraries before compiling:
//   - WiFiManager (by tzapu)
//   - ArduinoModbus (by Arduino) - pulls in ArduinoRS485 automatically
//   - LiquidCrystal_I2C (by Frank de Brabander / John Rickman)

#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoModbus.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ---------------- Fixed hardware pins ----------------
static const int POT_PIN = 34; // ADC1 input-only pin, safe to read with WiFi active
static const int LCD_SDA = 21;
static const int LCD_SCL = 22;
static const uint8_t LCD_ADDR = 0x27; // common PCF8574 backpack address; try 0x3F if the display stays blank
static const int WIFI_RESET_BUTTON_PIN = 0; // BOOT button on most ESP32 dev boards

static const char *AP_PASSWORD = "smasetup"; // shown on the LCD while in setup mode

// ---------------- Timing ----------------
static const unsigned long READ_INTERVAL_MS        = 5000;
static const unsigned long ENERGY_READ_INTERVAL_MS  = 60000;
static const unsigned long WRITE_MIN_INTERVAL_MS    = 2000;
static const unsigned long WIFI_RETRY_INTERVAL_MS   = 5000;
static const uint8_t       POT_DEADBAND_PERCENT     = 2;
static const uint8_t       POT_SAMPLES              = 16; // averaged per reading

// ---------------- Persisted settings ----------------
struct Settings {
  char     smaIp[16]        = "192.168.1.50";
  uint16_t smaPort           = 502;
  uint8_t  smaUnitId         = 3;
  uint32_t regAcPower        = 30775; // GridMs.TotW, input, S32
  uint32_t regEnergyTotal    = 30513; // Metering.TotWhOut, input, U64
  uint32_t regPowerLimit     = 41255; // active power limit %, holding, int16
  uint16_t powerLimitScale   = 100;   // raw register value = percent * scale
  bool     writeEnabled      = false;
};

Settings settings;
Preferences prefs;

void loadSettings() {
  prefs.begin("sma", true);
  if (prefs.getBytesLength("cfg") != sizeof(settings)) {
    prefs.end();
    prefs.begin("sma", false);
    prefs.putBytes("cfg", &settings, sizeof(settings)); // first boot: store defaults
    prefs.end();
    return;
  }
  prefs.getBytes("cfg", &settings, sizeof(settings));
  prefs.end();
}

void saveSettings() {
  prefs.begin("sma", false);
  prefs.putBytes("cfg", &settings, sizeof(settings));
  prefs.end();
}

// Parses a decimal string; keeps `fallback` if the string is empty/null.
uint32_t parseU32(const char *val, uint32_t fallback) {
  if (val == nullptr || val[0] == '\0') return fallback;
  return (uint32_t)strtoul(val, nullptr, 10);
}

// ---------------- Globals ----------------
LiquidCrystal_I2C lcd(LCD_ADDR, 20, 4);
WiFiClient wifiClient;
ModbusTCPClient modbus(wifiClient);
WebServer server(80);

unsigned long lastReadMs       = 0;
unsigned long lastEnergyReadMs = 0;
unsigned long lastWriteMs      = 0;
unsigned long lastWifiTryMs    = 0;

bool     modbusConnected = false;
int32_t  lastAcPowerW    = 0;
bool     haveAcPower      = false;
uint64_t lastEnergyWh     = 0;
bool     haveEnergy        = false;
uint8_t  lastSentPercent   = 255; // 255 = never sent yet
uint8_t  currentPotPercent = 0;

// ---------------- Modbus helpers ----------------
bool ensureModbus() {
  if (WiFi.status() != WL_CONNECTED) {
    modbusConnected = false;
    return false;
  }
  if (modbusConnected && wifiClient.connected()) {
    return true;
  }
  IPAddress ip;
  if (!ip.fromString(settings.smaIp)) {
    Serial.println(F("Modbus: invalid inverter IP configured"));
    return false;
  }
  modbusConnected = modbus.begin(ip, settings.smaPort);
  if (!modbusConnected) {
    Serial.println(F("Modbus: connect failed"));
  }
  return modbusConnected;
}

bool readS32InputRegister(uint32_t address, int32_t &result) {
  if (!modbus.requestFrom(settings.smaUnitId, INPUT_REGISTERS, address, 2)) {
    modbusConnected = false;
    return false;
  }
  uint32_t hi = (uint16_t)modbus.read();
  uint32_t lo = (uint16_t)modbus.read();
  result = (int32_t)((hi << 16) | lo);
  return true;
}

bool readU64InputRegister(uint32_t address, uint64_t &result) {
  if (!modbus.requestFrom(settings.smaUnitId, INPUT_REGISTERS, address, 4)) {
    modbusConnected = false;
    return false;
  }
  uint64_t value = 0;
  for (uint8_t i = 0; i < 4; i++) {
    value = (value << 16) | (uint16_t)modbus.read();
  }
  result = value;
  return true;
}

bool writeInt16HoldingRegister(uint32_t address, int16_t value) {
  if (!modbus.beginTransmission(settings.smaUnitId, HOLDING_REGISTERS, address, 1)) {
    modbusConnected = false;
    return false;
  }
  modbus.write((uint16_t)value);
  if (!modbus.endTransmission()) {
    modbusConnected = false;
    return false;
  }
  return true;
}

// ---------------- Potentiometer ----------------
uint8_t readPotPercent() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < POT_SAMPLES; i++) {
    sum += analogRead(POT_PIN);
    delay(1);
  }
  uint32_t avg = sum / POT_SAMPLES;
  return (uint8_t)constrain((avg * 100UL) / 4095UL, 0UL, 100UL);
}

// ---------------- Display (20x4 I2C LCD) ----------------
void lcdLine(uint8_t row, String text) {
  if (text.length() > 20) {
    text = text.substring(0, 20);
  }
  while (text.length() < 20) {
    text += ' ';
  }
  lcd.setCursor(0, row);
  lcd.print(text);
}

void showSetupScreen(const String &apName) {
  lcdLine(0, F("Setup mode - verbind:"));
  lcdLine(1, apName);
  lcdLine(2, String(F("Wachtwoord: ")) + AP_PASSWORD);
  lcdLine(3, F("http://192.168.4.1"));
}

void updateDisplay() {
  String line0 = F("WiFi:");
  line0 += (WiFi.status() == WL_CONNECTED) ? F("OK") : F("..");
  line0 += F(" MB:");
  line0 += modbusConnected ? F("OK") : F("..");
  lcdLine(0, line0);

  lcdLine(1, (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String(F("niet verbonden")));

  String line2 = F("P:");
  line2 += haveAcPower ? (String(lastAcPowerW) + F("W")) : String(F("--"));
  line2 += F(" E:");
  line2 += haveEnergy ? (String(lastEnergyWh / 1000.0, 1) + F("kWh")) : String(F("--"));
  lcdLine(2, line2);

  String line3 = F("Limiet: ");
  line3 += currentPotPercent;
  line3 += F("%");
  if (!settings.writeEnabled) {
    line3 += F(" (RO)");
  }
  lcdLine(3, line3);
}

// ---------------- Web config UI ----------------
bool checkAuth() {
  if (!server.authenticate("admin", "sma1234")) {
    server.requestAuthentication();
    return false;
  }
  return true;
}

String htmlPage(const String &body) {
  String page = F("<!DOCTYPE html><html><head><meta name='viewport' "
                   "content='width=device-width,initial-scale=1'>"
                   "<title>SMA controller</title>"
                   "<style>body{font-family:sans-serif;max-width:480px;margin:2em auto;padding:0 1em}"
                   "label{display:block;margin-top:1em}input{width:100%;padding:.4em;box-sizing:border-box}"
                   "button{margin-top:1.5em;padding:.6em 1.2em}"
                   ".row{display:flex;justify-content:space-between;margin:.3em 0}</style></head><body>");
  page += body;
  page += F("</body></html>");
  return page;
}

void handleRoot() {
  String body = F("<h2>SMA Sunny Boy controller</h2><div class='row'><span>WiFi</span><span>");
  if (WiFi.status() == WL_CONNECTED) {
    body += F("verbonden (");
    body += WiFi.localIP().toString();
    body += F(")");
  } else {
    body += F("niet verbonden");
  }
  body += F("</span></div><div class='row'><span>Modbus</span><span>");
  body += modbusConnected ? F("verbonden") : F("niet verbonden");
  body += F("</span></div><div class='row'><span>AC vermogen</span><span>");
  body += haveAcPower ? (String(lastAcPowerW) + F(" W")) : String(F("--"));
  body += F("</span></div><div class='row'><span>Totaal energie</span><span>");
  body += haveEnergy ? (String(lastEnergyWh / 1000.0, 1) + F(" kWh")) : String(F("--"));
  body += F("</span></div><div class='row'><span>Potmeter</span><span>");
  body += String(currentPotPercent);
  body += F("%</span></div><p><a href='/config'>Instellingen</a></p>");
  server.send(200, "text/html", htmlPage(body));
}

void handleConfigGet() {
  if (!checkAuth()) return;
  String body = F("<h2>Instellingen</h2><form method='POST' action='/config'>");

  body += F("<label>IP-adres omvormer<input name='ip' value='");
  body += settings.smaIp;
  body += F("'></label>");

  body += F("<label>Modbus TCP poort<input name='port' type='number' value='");
  body += settings.smaPort;
  body += F("'></label>");

  body += F("<label>Modbus unit ID<input name='unit' type='number' value='");
  body += settings.smaUnitId;
  body += F("'></label>");

  body += F("<label>Register AC-vermogen (input)<input name='reg_ac' type='number' value='");
  body += settings.regAcPower;
  body += F("'></label>");

  body += F("<label>Register totaal energie (input)<input name='reg_energy' type='number' value='");
  body += settings.regEnergyTotal;
  body += F("'></label>");

  body += F("<label>Register vermogenslimiet (holding)<input name='reg_limit' type='number' value='");
  body += settings.regPowerLimit;
  body += F("'></label>");

  body += F("<label>Schaal vermogenslimiet (raw = % * schaal)<input name='scale' type='number' value='");
  body += settings.powerLimitScale;
  body += F("'></label>");

  body += F("<label><input name='write_enabled' type='checkbox' style='width:auto' ");
  body += settings.writeEnabled ? F("checked") : F("");
  body += F("> Vermogenslimiet daadwerkelijk naar omvormer schrijven</label>");
  body += F("<button type='submit'>Opslaan &amp; herstarten</button></form>");
  body += F("<form method='POST' action='/forget-wifi' onsubmit=\"return confirm('WiFi-gegevens wissen en setup-portal openen?');\">"
             "<button type='submit'>WiFi vergeten / opnieuw instellen</button></form>");
  server.send(200, "text/html", htmlPage(body));
}

void handleConfigPost() {
  if (!checkAuth()) return;
  if (server.hasArg("ip")) {
    strncpy(settings.smaIp, server.arg("ip").c_str(), sizeof(settings.smaIp) - 1);
    settings.smaIp[sizeof(settings.smaIp) - 1] = '\0';
  }
  if (server.hasArg("port")) settings.smaPort = server.arg("port").toInt();
  if (server.hasArg("unit")) settings.smaUnitId = server.arg("unit").toInt();
  if (server.hasArg("reg_ac")) settings.regAcPower = server.arg("reg_ac").toInt();
  if (server.hasArg("reg_energy")) settings.regEnergyTotal = server.arg("reg_energy").toInt();
  if (server.hasArg("reg_limit")) settings.regPowerLimit = server.arg("reg_limit").toInt();
  if (server.hasArg("scale")) settings.powerLimitScale = server.arg("scale").toInt();
  settings.writeEnabled = server.hasArg("write_enabled");
  saveSettings();

  server.send(200, "text/html", htmlPage(F("<p>Opgeslagen, apparaat herstart...</p>")));
  delay(500);
  ESP.restart();
}

void handleForgetWifi() {
  if (!checkAuth()) return;
  server.send(200, "text/html", htmlPage(F("<p>WiFi-gegevens gewist, apparaat herstart in setup-modus...</p>")));
  delay(500);
  WiFiManager wm;
  wm.resetSettings();
  ESP.restart();
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/config", HTTP_GET, handleConfigGet);
  server.on("/config", HTTP_POST, handleConfigPost);
  server.on("/forget-wifi", HTTP_POST, handleForgetWifi);
  server.begin();
}

// ---------------- WiFi / setup portal ----------------
String g_apName; // set just before autoConnect(); read by apCallback()

void apCallback(WiFiManager *) {
  showSetupScreen(g_apName);
}

void runCaptivePortal() {
  WiFiManager wm;
  wm.setConfigPortalTimeout(300);

  char ipBuf[16];
  strncpy(ipBuf, settings.smaIp, sizeof(ipBuf));
  char portBuf[6];
  snprintf(portBuf, sizeof(portBuf), "%u", settings.smaPort);
  char unitBuf[4];
  snprintf(unitBuf, sizeof(unitBuf), "%u", settings.smaUnitId);
  char regAcBuf[8];
  snprintf(regAcBuf, sizeof(regAcBuf), "%lu", (unsigned long)settings.regAcPower);
  char regEnergyBuf[8];
  snprintf(regEnergyBuf, sizeof(regEnergyBuf), "%lu", (unsigned long)settings.regEnergyTotal);
  char regLimitBuf[8];
  snprintf(regLimitBuf, sizeof(regLimitBuf), "%lu", (unsigned long)settings.regPowerLimit);

  WiFiManagerParameter customSmaIp("sma_ip", "IP-adres van de SMA omvormer", ipBuf, sizeof(ipBuf));
  WiFiManagerParameter customPort("sma_port", "Modbus TCP poort", portBuf, sizeof(portBuf), "type='number'");
  WiFiManagerParameter customUnit("sma_unit", "Modbus unit ID", unitBuf, sizeof(unitBuf), "type='number'");
  WiFiManagerParameter customRegAc("reg_ac", "Register AC-vermogen (input)", regAcBuf, sizeof(regAcBuf), "type='number'");
  WiFiManagerParameter customRegEnergy("reg_energy", "Register totaal energie (input)", regEnergyBuf, sizeof(regEnergyBuf), "type='number'");
  WiFiManagerParameter customRegLimit("reg_limit", "Register vermogenslimiet (holding)", regLimitBuf, sizeof(regLimitBuf), "type='number'");

  wm.addParameter(&customSmaIp);
  wm.addParameter(&customPort);
  wm.addParameter(&customUnit);
  wm.addParameter(&customRegAc);
  wm.addParameter(&customRegEnergy);
  wm.addParameter(&customRegLimit);

  g_apName = "SMA-Setup-" + String((uint32_t)(ESP.getEfuseMac() & 0xFFFFFF), HEX);
  wm.setAPCallback(apCallback);

  bool connected = wm.autoConnect(g_apName.c_str(), AP_PASSWORD);
  if (!connected) {
    Serial.println(F("WiFi: setup portal timed out, restarting"));
    ESP.restart();
  }

  strncpy(settings.smaIp, customSmaIp.getValue(), sizeof(settings.smaIp) - 1);
  settings.smaIp[sizeof(settings.smaIp) - 1] = '\0';
  settings.smaPort = (uint16_t)parseU32(customPort.getValue(), settings.smaPort);
  settings.smaUnitId = (uint8_t)parseU32(customUnit.getValue(), settings.smaUnitId);
  settings.regAcPower = parseU32(customRegAc.getValue(), settings.regAcPower);
  settings.regEnergyTotal = parseU32(customRegEnergy.getValue(), settings.regEnergyTotal);
  settings.regPowerLimit = parseU32(customRegLimit.getValue(), settings.regPowerLimit);
  saveSettings();
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin(LCD_SDA, LCD_SCL);
  lcd.init();
  lcd.backlight();
  lcd.clear();

  analogReadResolution(12);
  pinMode(WIFI_RESET_BUTTON_PIN, INPUT_PULLUP);

  loadSettings();

  // Hold the BOOT button for 3s at power-up to wipe WiFi + re-open the portal.
  if (digitalRead(WIFI_RESET_BUTTON_PIN) == LOW) {
    unsigned long start = millis();
    while (digitalRead(WIFI_RESET_BUTTON_PIN) == LOW && millis() - start < 3000) {
      delay(10);
    }
    if (millis() - start >= 3000) {
      WiFiManager wm;
      wm.resetSettings();
    }
  }

  runCaptivePortal();

  String hostname = "sma-ctrl-" + String((uint32_t)(ESP.getEfuseMac() & 0xFFF), HEX);
  MDNS.begin(hostname.c_str());
  MDNS.addService("http", "tcp", 80);
  setupWebServer();
}

void loop() {
  server.handleClient();

  if (WiFi.status() != WL_CONNECTED && millis() - lastWifiTryMs >= WIFI_RETRY_INTERVAL_MS) {
    lastWifiTryMs = millis();
    WiFi.reconnect();
  }

  currentPotPercent = readPotPercent();

  if (ensureModbus()) {
    unsigned long now = millis();

    if (now - lastReadMs >= READ_INTERVAL_MS) {
      lastReadMs = now;
      haveAcPower = readS32InputRegister(settings.regAcPower, lastAcPowerW);
    }

    if (now - lastEnergyReadMs >= ENERGY_READ_INTERVAL_MS) {
      lastEnergyReadMs = now;
      haveEnergy = readU64InputRegister(settings.regEnergyTotal, lastEnergyWh);
    }

    if (settings.writeEnabled) {
      bool changedEnough =
        lastSentPercent == 255 ||
        (uint8_t)abs((int)currentPotPercent - (int)lastSentPercent) >= POT_DEADBAND_PERCENT;

      if (changedEnough && millis() - lastWriteMs >= WRITE_MIN_INTERVAL_MS) {
        lastWriteMs = millis();
        int16_t raw = (int16_t)((int32_t)currentPotPercent * settings.powerLimitScale);
        if (writeInt16HoldingRegister(settings.regPowerLimit, raw)) {
          lastSentPercent = currentPotPercent;
        }
      }
    }
  }

  updateDisplay();
  delay(200);
}
