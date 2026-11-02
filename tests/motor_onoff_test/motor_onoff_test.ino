// Motor on/off test: GPIO 3 -> 220 ohm -> 2N2222 base.
// Motor should run 2 s, stop 2 s, repeat.
#define PWM_PIN 3

void setup() {
  ledcAttach(PWM_PIN, 1000, 8);
}

void loop() {
  ledcWrite(PWM_PIN, 255);  // full on
  delay(2000);
  ledcWrite(PWM_PIN, 0);    // off
  delay(2000);
}
