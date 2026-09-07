# SMA Sunny Boy WiFi/Modbus controller (ESP32)

Standalone ESP32 project (not an ESPEasy plugin — this repo's ESPEasy
checkout predates ESP32 support entirely). Reads the current AC power
from an SMA Sunny Boy inverter over Modbus TCP and lets you set an
active-power-limit setpoint with a potentiometer, shown on a small
OLED display.

## Hardware

- ESP32 dev board
- Potentiometer: outer legs to 3V3 and GND, wiper to GPIO34 (`POT_PIN`)
- SSD1306 128x64 I2C OLED: SDA to GPIO21, SCL to GPIO22 (`OLED_SDA`/`OLED_SCL`)
- Same WiFi network as the SMA inverter

## Setup

1. `cp src/config.example.h src/config.h` and fill in your WiFi
   credentials and the inverter's IP address.
2. On the inverter (local webUI or Sunny Portal): enable Modbus TCP
   under Settings → Communication, and note the IP, port (default
   502) and unit ID (default 3, some models use 1).
3. Build/flash with PlatformIO: `pio run -t upload` (from this
   directory), `pio device monitor` for logs.

## Verify your registers before writing anything

`REG_AC_POWER` (30775, `GridMs.TotW`) is a standard SMA Modbus
register used for reading total AC power on virtually all SMA grid
inverters — safe to use as shipped.

`REG_POWER_LIMIT`, the register that actually sets the active power
limit / curtailment setpoint, is **not** filled in by default. Its
address, scaling, and whether it needs a Grid Guard / installer login
differ by inverter model and firmware. Look it up in SMA's "Modbus
parameters and measured values" list for your exact device, fill in
`REG_POWER_LIMIT` in `config.h`, and only then set `WRITE_ENABLED 1`.
Until you do, the firmware runs read-only: the potmeter position is
shown on the display but nothing is sent to the inverter.

## Behavior

- Reads `REG_AC_POWER` every 5 seconds.
- Samples the potmeter continuously (averaged over 16 ADC reads).
- With `WRITE_ENABLED 1`: writes the setpoint to `REG_POWER_LIMIT`
  whenever it moves by more than 2%, at most once every 2 seconds.
- OLED shows WiFi status, Modbus connection status, measured AC
  power, and the current setpoint.
