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

## Registers

| Register | Address | Type | Meaning |
|---|---|---|---|
| `REG_AC_POWER` | 30775 | input, S32 | `GridMs.TotW` — total AC power (W), standard on virtually all SMA grid inverters |
| `REG_ENERGY_TOTAL` | 30513 | input, U64 | `Metering.TotWhOut` — lifetime energy fed in (Wh) |
| `REG_POWER_LIMIT` | 41255 | holding, int16, scale 0.01 | Active power limit setpoint in % (raw 5000 = 50.00%) |

`REG_AC_POWER` and `REG_ENERGY_TOTAL` are safe to read as shipped.
`REG_POWER_LIMIT` still varies by inverter model/firmware, and some
models need a Grid Guard / installer login before they'll accept
writes to it — double check against your own inverter before flipping
`WRITE_ENABLED` to `1`. Until then the firmware runs read-only: the
potmeter position is shown on the display but nothing is sent to the
inverter.

## Behavior

- Reads `REG_AC_POWER` every 5 seconds and `REG_ENERGY_TOTAL` every 60.
- Samples the potmeter continuously (averaged over 16 ADC reads).
- With `WRITE_ENABLED 1`: writes the setpoint to `REG_POWER_LIMIT`
  (scaled by `REG_POWER_LIMIT_SCALE`) whenever it moves by more than
  2%, at most once every 2 seconds.
- OLED shows WiFi status, Modbus connection status, measured AC
  power, lifetime energy, and the current setpoint.
