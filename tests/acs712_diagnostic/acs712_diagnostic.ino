// ACS712 diagnostic: prints the divider node voltage with the motor off and on.
// Idle (OFF) node reading should be about 1200-1300 mV with a 2-resistor
// equal divider. About 100 mV means OUT is not reaching GPIO 1.
#define PWM_PIN 3
#define ACS_PIN 1

int avgMv() {
  long s = 0;
  for (int i = 0; i < 50; i++) { s += analogReadMilliVolts(ACS_PIN); delay(2); }
  return s / 50;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  ledcAttach(PWM_PIN, 1000, 8);
  Serial.println("Test: 3 s OFF, then 1.5 s ON");
}

void loop() {
  ledcWrite(PWM_PIN, 0);
  delay(3000);
  int off = avgMv();
  Serial.printf("OFF: ACS node = %d mV\n", off);

  ledcWrite(PWM_PIN, 255);
  delay(1000);
  int on = avgMv();
  Serial.printf("ON : ACS node = %d mV  (change = %d mV)\n", on, on - off);
  delay(500);
}
