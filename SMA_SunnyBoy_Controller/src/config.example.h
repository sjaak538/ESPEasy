#pragma once
// Copy this file to config.h and fill in your own values.
// config.h is gitignored so your WiFi password never gets committed.

// ---------------- WiFi ----------------
#define WIFI_SSID     "YOUR_SSID"
#define WIFI_PASSWORD "YOUR_PASSWORD"

// ---------------- SMA inverter (Modbus TCP server) ----------------
// Enable "Modbus" under the inverter's local webUI / Sunny Portal ->
// Settings -> Communication, note the IP and unit ID shown there.
#define SMA_IP      IPAddress(192, 168, 1, 50)
#define SMA_PORT    502
#define SMA_UNIT_ID 3   // SMA default Modbus unit id (some models use 1)

// ---------------- Registers ----------------
// REG_AC_POWER: "GridMs.TotW" (total AC active power), a standard,
// widely-documented register in SMA's public Modbus profile:
//   Input register, function code 04, S32 (2 registers, big-endian),
//   gain 1, unit W.
// This one is safe to read as-is on virtually all SMA grid inverters.
#define REG_AC_POWER       30775
#define REG_AC_POWER_COUNT 2

// REG_POWER_LIMIT: holding register for the active power limit /
// curtailment setpoint, as reported for this inverter: single int16
// (some models use uint16, doesn't matter here since values stay
// 0-10000, positive) register, scale 0.01 -> raw 5000 means 50.00%.
// Still verify against your own inverter before flipping WRITE_ENABLED;
// the address/scale can differ on other SMA models/firmware.
#define REG_POWER_LIMIT       41255
#define REG_POWER_LIMIT_SCALE 100   // raw register value = percent * 100

// Stays 0 (read-only mode) until you're sure REG_POWER_LIMIT is right
// for your inverter. With this at 0 the potmeter position is only
// shown on the display, nothing is written.
#define WRITE_ENABLED 0

// REG_ENERGY_TOTAL: "Metering.TotWhOut", total lifetime AC energy fed
// in. Input register, U64 (4 registers, big-endian), unit Wh.
#define REG_ENERGY_TOTAL 30513

// ---------------- Hardware pins ----------------
#define POT_PIN     34   // ADC1 input-only pin, safe to read with WiFi active
#define OLED_SDA    21
#define OLED_SCL    22
#define OLED_ADDR   0x3C
