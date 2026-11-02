# Wiring

## Rails
| Rail | Source |
|---|---|
| 3.3 V | ESP32 3V3 |
| 5 V (sensor) | ESP32 5V pin (ACS712 VCC only) |
| GND | ESP32 GND, **shared with the motor battery minus** |

## MPU6050 and LCD (shared I2C pair)
| Pin | Connects to |
|---|---|
| MPU6050 VCC / GND | 3.3 V / GND |
| MPU6050 SDA / SCL | GPIO 6 / GPIO 7 |
| LCD VCC / GND | 3.3 V / GND |
| LCD SDA / SCL | GPIO 6 / GPIO 7 (same points as MPU6050) |

The MPU6050 is at address 0x68; the LCD is usually 0x27 or 0x3F.
Fix the MPU6050 rigidly to the motor body.

## Potentiometer
| Pot pin | Connects to |
|---|---|
| Outer 1 | 3.3 V |
| Wiper | GPIO 0 |
| Outer 2 | GND |

## ACS712 signal side
| Pin | Connects to |
|---|---|
| VCC | ESP32 5V |
| GND | GND |
| OUT | R1 (8.4 kohm) |

```
ACS712 OUT --[ R1 ]--+---- GPIO 1
                     |
                   [ R2 ]
                     |
                    GND
```
R1 = R2 gives `DIV_RATIO 0.500`. Idle reading at GPIO 1 should be about 1250 mV.

## Motor power path
```
Battery (+) -> [switch] -> ACS712 IP+
ACS712 IP-  -> Motor +
Motor -     -> 2N2222 Collector
2N2222 Emitter -> GND
Battery (-) -> GND (common with ESP32)
```
| Part | Connection |
|---|---|
| Diode (1N4007 / 1N5819) | Across the motor, stripe on motor + |
| 220 ohm | GPIO 3 to 2N2222 base |
| 8.4 kohm (optional) | 2N2222 base to GND |

Check the 2N2222 pin order against your part's datasheet (E-B-C versus C-B-E).

## Before power-up
1. ACS712 IP+/IP- are in series with the motor, not across it.
2. Diode stripe faces motor +.
3. Motor battery (+) touches only the switch and IP+.
4. All grounds are joined.
