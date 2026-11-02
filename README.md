# Kaadhu 2.0 - Texture-Aware Grinder Prototype

AtomQuest 2026 | Track 3: Mixer Grinder | Primary area: Grind & Texture

A low-voltage test rig that estimates grinding progress from motor current,
vibration and energy use, compares it with a saved reference profile, and
shuts the motor off on overload or high temperature.

## Status

Early prototype. All performance figures in the abstract are proposed
evaluation targets and have **not** been validated yet. The progress estimate
is a hypothesis to be tested against sieve results; when the energy-based and
vibration-based estimates disagree, the firmware reports low confidence
instead of claiming a texture prediction.

## How it works

1. Run a reference batch (fixed ingredient, fixed mass, fixed pot position)
   until the sieve test says the texture is right, then save it with `c`.
2. In later runs the firmware compares live energy and vibration against that
   profile and prints guidance: CONTINUE, NEAR, STOP or LOW-CONF.
3. Overload/stall and over-temperature trip a software cutoff.

## Hardware

| Part | Role |
|---|---|
| ESP32-C6 DevKit | Controller |
| MPU6050 | Vibration sensing, temperature proxy |
| ACS712 5A module | Motor current |
| 5 V DC toy motor + 2N2222 | Test load and switch |
| 10k potentiometer | Speed setting |
| I2C 16x2 LCD (optional) | Status display |
| Separate 5 V battery | Motor supply (common ground with ESP32) |

Wiring: see [docs/wiring.md](docs/wiring.md)

## Pin map

| ESP32-C6 | Function |
|---|---|
| GPIO 0 | Pot wiper |
| GPIO 1 | ACS712 divider midpoint |
| GPIO 3 | 2N2222 base (via 220 ohm) |
| GPIO 6 / 7 | I2C SDA / SCL (MPU6050 and LCD) |

## Build and run

1. Arduino IDE 2.x with the ESP32 board package (3.x).
2. Install the library **LiquidCrystal I2C** (Frank de Brabander).
3. Board: ESP32C6 Dev Module, **USB CDC On Boot: Enabled**.
4. Open `firmware/kaadhu_prototype/kaadhu_prototype.ino` and upload.
5. Serial Monitor: 115200 baud, "No Line Ending".

## Serial commands

| Key | Action |
|---|---|
| `s` | Start run (motor on, pot sets speed) |
| `e` | End run |
| `c` | Save current run as the reference profile |
| `p` | Show saved profile |
| `x` | Clear saved profile |
| `z` | Re-zero the current sensor (motor off) |
| `r` | Reset a fault |

## Bring-up order

1. `tests/mpu6050_test` - confirm WHO_AM_I and live readings.
2. `tests/motor_onoff_test` - confirm the 2N2222 switches the motor.
3. `tests/acs712_diagnostic` - the idle node reading should be about 1250 mV.
4. Main firmware.

## Settings to check in the firmware

| Setting | Meaning |
|---|---|
| `SUPPLY_V` | Motor supply voltage while running (energy is an estimate) |
| `ACS_SENS` | 0.185 for 5A, 0.100 for 20A, 0.066 for 30A modules |
| `DIV_RATIO` | R2 / (R1 + R2) of the ACS712 output divider |
| `I_LIMIT_A` | Overload trip, keep below the 2N2222 rating (about 0.6 A) |
| `LCD_ADDR` | 0x27, or 0x3F on some modules |

## Known limitations

- A 5 V toy motor has very little torque; it validates the sensing, safety and
  profile pipeline, not real spice grinding.
- MPU6050 temperature is a rough proxy, not motor temperature.
- Energy assumes a constant supply voltage.
- The software cutoff is not an independent hardware cutoff.

## Safety

Low-voltage rig only. Do not connect mains. Use a physical switch or fuse in
the motor supply line, and a guarded test setup.

## License

MIT - see [LICENSE](LICENSE).
