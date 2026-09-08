// Minimal WiFi test for an ESP32 (Wemos D1 R32 / "Uno" form factor).
// No WiFiManager, no LCD, no Modbus - just the bare ESP32 WiFi radio,
// to isolate whether the chip/board can bring up a network at all.
//
// Board: Tools -> Board -> search "WEMOS D1 R32" (falls back fine to
// plain "ESP32 Dev Module" too, since it's the same chip/radio).
// No extra libraries needed - only <WiFi.h>, built into the esp32 core.
//
// What it does:
//   1. Scans for nearby WiFi networks and prints them (tests STA/radio).
//   2. Starts a SoftAP "ESP32-Test" / password "12345678".
//   3. Every 2s prints its own IP and how many stations are connected,
//      so you can watch from the Serial Monitor whether the AP is
//      alive even if your phone/laptop can't see it.

#include <WiFi.h>

const char *AP_SSID = "ESP32-Test";
const char *AP_PASSWORD = "12345678"; // 8+ chars required for WPA2

unsigned long lastStatusMs = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println(F("=== ESP32 WiFi test ==="));

  Serial.print(F("Chip model: "));
  Serial.println(ESP.getChipModel());
  Serial.print(F("MAC address: "));
  Serial.println(WiFi.macAddress());

  Serial.println(F("Scanning for nearby WiFi networks..."));
  WiFi.mode(WIFI_MODE_STA);
  int n = WiFi.scanNetworks();
  if (n <= 0) {
    Serial.println(F("Scan found 0 networks - radio may not be working."));
  } else {
    Serial.print(F("Scan found "));
    Serial.print(n);
    Serial.println(F(" networks:"));
    for (int i = 0; i < n; i++) {
      Serial.print(F("  "));
      Serial.print(WiFi.SSID(i));
      Serial.print(F(" ("));
      Serial.print(WiFi.RSSI(i));
      Serial.println(F(" dBm)"));
    }
  }

  Serial.println(F("Starting SoftAP..."));
  WiFi.mode(WIFI_MODE_AP);
  bool ok = WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print(F("softAP() returned: "));
  Serial.println(ok ? F("true") : F("false"));
  Serial.print(F("AP IP address: "));
  Serial.println(WiFi.softAPIP());
  Serial.print(F("SSID: "));
  Serial.println(AP_SSID);
  Serial.print(F("Password: "));
  Serial.println(AP_PASSWORD);
  Serial.println(F("Look for this network on your phone/laptop now."));
}

void loop() {
  if (millis() - lastStatusMs >= 2000) {
    lastStatusMs = millis();
    Serial.print(F("[status] AP IP="));
    Serial.print(WiFi.softAPIP());
    Serial.print(F("  stations connected="));
    Serial.println(WiFi.softAPgetStationNum());
  }
}
