# Arduino IDE versie

Zelfde firmware als `../src/main.cpp` (PlatformIO), maar dan als kant-en-klare
Arduino IDE-sketch: `SMA_SunnyBoy_Controller/SMA_SunnyBoy_Controller.ino`.

## Openen

Open in de Arduino IDE via **File → Open...** en wijs naar
`SMA_SunnyBoy_Controller.ino` in deze map (niet de losse map
`ArduinoIDE` zelf, maar de submap die dezelfde naam heeft als het
`.ino`-bestand — dat vereist de Arduino IDE nu eenmaal).

## Board-instellingen

- **Tools → Board**: "ESP32 Dev Module" (uit de `esp32`-boardpakket
  van Espressif Systems — installeer die eerst via Boards Manager als
  dat nog niet is gebeurd).
- Overige instellingen (Flash size, Partition scheme, Upload speed)
  mogen op de standaardwaarden blijven voor een gewone WROOM-32.

## Libraries installeren

Via **Sketch → Include Library → Manage Libraries**, zoek en installeer:

- **WiFiManager** (door tzapu)
- **ArduinoModbus** (door Arduino) — installeert automatisch ook
  **ArduinoRS485** als afhankelijkheid
- **LiquidCrystal_I2C** (door Frank de Brabander / John Rickman)

Daarna gewoon **Verify/Compile** en **Upload**. Verdere werking en
bedrading: zie de hoofd-`README.md` een map hoger.
