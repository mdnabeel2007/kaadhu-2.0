// MPU6050 test on ESP32-C6: SDA = GPIO 6, SCL = GPIO 7.
// Flat on the table: Z about 1.00 g, X/Y near 0.
#include <Wire.h>

#define SDA_PIN 6
#define SCL_PIN 7
#define MPU_ADDR 0x68

int16_t rd16() {
  uint8_t h = Wire.read();
  uint8_t l = Wire.read();
  return (int16_t)((h << 8) | l);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting...");
  Wire.begin(SDA_PIN, SCL_PIN);

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0x00);          // wake up
  Wire.endTransmission();

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x75);          // WHO_AM_I
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)1);
  Serial.printf("WHO_AM_I = 0x%02X (expected 0x68)\n", Wire.read());
}

void loop() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)14);

  int16_t ax = rd16(), ay = rd16(), az = rd16();
  int16_t temp = rd16();
  int16_t gx = rd16(), gy = rd16(), gz = rd16();

  Serial.printf("Accel: %.2f %.2f %.2f g | Gyro: %.1f %.1f %.1f deg/s | Temp: %.1f C\n",
                ax / 16384.0, ay / 16384.0, az / 16384.0,
                gx / 131.0, gy / 131.0, gz / 131.0,
                temp / 340.0 + 36.53);
  delay(500);
}
