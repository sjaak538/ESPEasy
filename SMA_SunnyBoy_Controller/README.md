# SMA Sunny Boy WiFi/Modbus controller (ESP32)

Standalone ESP32 project (not an ESPEasy plugin — this repo's ESPEasy
checkout predates ESP32 support entirely). Reads the current AC power
and lifetime energy from an SMA Sunny Boy inverter over Modbus TCP,
and lets you set an active-power-limit setpoint with a potentiometer,
shown on a small OLED display.

**One firmware image works for every install** — nothing is baked in
at compile time. WiFi and all SMA settings are configured after
flashing, through the device's own web pages, and are stored in flash
(NVS) so they survive reboots and reflashes.

## Hardware

- ESP32 dev board
- Potentiometer: outer legs to 3V3 and GND, wiper to GPIO34 (`POT_PIN`)
- SSD1306 128x64 I2C OLED: SDA to GPIO21, SCL to GPIO22 (`OLED_SDA`/`OLED_SCL`)

## First-time setup (per customer, no reflashing)

1. Flash the firmware once: `pio run -t upload` from this directory.
2. Power it on. With no WiFi configured yet it starts a setup access
   point — the OLED shows the network name and password directly:
   `SMA-Setup-xxxxxx` / `smasetup`.
3. Connect a phone or laptop to that network. A configuration page
   should open automatically (captive portal); if not, browse to
   `http://192.168.4.1`.
4. Pick the customer's WiFi network, enter its password, and fill in
   the SMA inverter's IP address, then save. The device reboots and
   joins that network.
5. Open `http://<device-ip>/` (also reachable via
   `http://sma-ctrl-xxxx.local/` on networks that support mDNS) and go
   to **Instellingen** to fine-tune the Modbus port/unit ID, registers
   and the write-enable switch. Default login: `admin` / `sma1234`.

To reconfigure WiFi later (e.g. moving the device to a different
customer): hold the BOOT button for 3 seconds at power-up, or use the
**WiFi vergeten** button on the settings page — both wipe the stored
WiFi credentials and reopen the setup portal.

## Registers

| Setting | Default | Type | Meaning |
|---|---|---|---|
| AC power register | 30775 | input, S32 | `GridMs.TotW` — total AC power (W), standard on virtually all SMA grid inverters |
| Energy total register | 30513 | input, U64 | `Metering.TotWhOut` — lifetime energy fed in (Wh) |
| Power limit register | 41255 | holding, int16, scale 0.01 | Active power limit setpoint in % (raw 5000 = 50.00%) |

All of these are editable per device on the **Instellingen** page —
no firmware changes needed if a particular inverter model/firmware
uses different addresses.

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
- OLED shows WiFi/Modbus status, device IP, measured AC power,
  lifetime energy, and the current setpoint.
- Web UI (`/`) shows the same status remotely; `/config` edits all
  SMA settings.
