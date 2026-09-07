# SMA Sunny Boy WiFi/Modbus controller (ESP32)

Standalone ESP32 project (not an ESPEasy plugin — this repo's ESPEasy
checkout predates ESP32 support entirely). Reads the current AC power
and lifetime energy from an SMA Sunny Boy inverter over Modbus TCP,
and lets you set an active-power-limit setpoint with a potentiometer,
shown on a 20x4 I2C LCD.

**One firmware image works for every install** — nothing is baked in
at compile time. WiFi and all SMA settings are configured after
flashing, through the device's own setup portal and web pages, and
are stored in flash (NVS) so they survive reboots and reflashes.

## Hardware

- ESP32 dev board
- Potentiometer: outer legs to 3V3 and GND, wiper to GPIO34 (`POT_PIN`)
- 2004 I2C LCD (PCF8574 backpack): SDA to GPIO21, SCL to GPIO22
  (`LCD_SDA`/`LCD_SCL`). Default I2C address `0x27` — if the display
  stays blank, try `0x3F` (the other common backpack address) in
  `LCD_ADDR`.

Using the Arduino IDE instead of PlatformIO? Use
`ArduinoIDE/SMA_SunnyBoy_Controller/SMA_SunnyBoy_Controller.ino`
instead of `src/main.cpp` — see `ArduinoIDE/README.md` for the board
and library setup.

## First-time setup (per customer, no reflashing)

1. Flash the firmware once: `pio run -t upload` from this directory
   (PlatformIO), or Verify + Upload the `.ino` sketch from the
   `ArduinoIDE/` folder.
2. Power it on. With no WiFi configured yet it starts a setup access
   point — the LCD shows the network name and password directly:
   `SMA-Setup-xxxxxx` / `smasetup`.
3. Connect a phone or laptop to that network. A configuration page
   should open automatically (captive portal); if not, browse to
   `http://192.168.4.1`.
4. Pick the customer's WiFi network, enter its password, and on the
   same page fill in the SMA inverter's IP address, Modbus port, unit
   ID and the three registers (AC power, energy total, power limit) —
   all pre-filled with sensible defaults, only the IP really needs to
   change for a typical install. Save, and the device reboots and
   joins that network.
5. Open `http://<device-ip>/` (also reachable via
   `http://sma-ctrl-xxxx.local/` on networks that support mDNS) and go
   to **Instellingen** if you still need to adjust the register scale
   or turn on writing. Default login: `admin` / `sma1234`.

To reconfigure WiFi and all SMA settings later (e.g. moving the device
to a different customer): hold the BOOT button for 3 seconds at
power-up, or use the **WiFi vergeten** button on the settings page —
both wipe the stored WiFi credentials and reopen the setup portal with
the fields above.

## Registers

| Setting | Default | Type | Meaning |
|---|---|---|---|
| AC power register | 30775 | input, S32 | `GridMs.TotW` — total AC power (W), standard on virtually all SMA grid inverters |
| Energy total register | 30513 | input, U64 | `Metering.TotWhOut` — lifetime energy fed in (Wh) |
| Power limit register | 41255 | holding, int16, scale 0.01 | Active power limit setpoint in % (raw 5000 = 50.00%) |

IP, port, unit ID and all three registers are editable right in the
setup portal; the register scale and write-enable switch are on the
**Instellingen** web page. No firmware changes needed if a particular
inverter model/firmware uses different addresses.

The AC power and energy registers are safe to read as shipped. The
power limit register still varies by inverter model/firmware, and
some models need a Grid Guard / installer login before they'll accept
writes to it — verify against the specific inverter before switching
on **"Vermogenslimiet daadwerkelijk naar omvormer schrijven"**. Until
that's on, the firmware runs read-only: the potmeter position is
shown on the display but nothing is sent to the inverter.

## Behavior

- Reads the AC power register every 5 seconds and the energy register
  every 60.
- Samples the potmeter continuously (averaged over 16 ADC reads).
- With writing enabled: writes the setpoint to the power limit
  register (scaled by the configured scale factor) whenever it moves
  by more than 2%, at most once every 2 seconds.
- LCD shows WiFi/Modbus status, device IP, measured AC power, lifetime
  energy, and the current setpoint.
- Web UI (`/`) shows the same status remotely; `/config` edits the
  remaining SMA settings.
