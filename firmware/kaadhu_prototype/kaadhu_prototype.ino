// Kaadhu 2.0 - texture-aware grinder prototype (ESP32-C6)
// AtomQuest 2026, Track 3. Low-voltage test rig only.
//
// Hardware: MPU6050 + I2C LCD on GPIO 6/7, ACS712 via 2-resistor divider on
// GPIO 1, pot on GPIO 0, motor switched by a 2N2222 on GPIO 3.
// Serial commands: s start, e end, c save profile, p show, x clear,
//                  z re-zero current sensor, r reset fault.

#include <Wire.h>
#include <Preferences.h>
#include <LiquidCrystal_I2C.h>

// ===== Pins =====
#define SDA_PIN 6
#define SCL_PIN 7
#define POT_PIN 0
#define ACS_PIN 1
#define PWM_PIN 3

// ===== Settings =====
#define MPU_ADDR      0x68
#define LCD_ADDR      0x27     // try 0x3F if the LCD stays blank
#define SUPPLY_V      5.0f     // motor supply voltage while running (e.g. 4.5 for 3xAA)
#define ACS_SENS      0.185f   // V/A: 5A module = 0.185, 20A = 0.100, 30A = 0.066
#define DIV_RATIO     0.500f   // R2 / (R1 + R2) of the ACS712 output divider
#define I_LIMIT_A     0.6f     // overload trip; keep below the 2N2222 rating
#define TEMP_LIMIT_C  65.0f    // MPU6050 die temperature (rough proxy)
#define OVERLOAD_MS   300
#define STARTUP_GRACE 1.0f     // seconds ignored for the start-up surge
#define MIN_DUTY      0.35f    // motor start threshold
#define PWM_FREQ      20000
#define PWM_BITS      10
#define WINDOW_MS     200

LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);
Preferences prefs;

float acsOffsetV = 2.5f;
bool running = false, fault = false;

double sumMv = 0, sumSq = 0;
uint32_t nSamp = 0, nAcc = 0;
float mx = 0, my = 0, mz = 0;

float duty = 0, I = 0, vib = 0, tempC = 0, nodeMv = 0;
float vibEma = 0, vibStart = 0;
bool vibStartSet = false;
double energyWh = 0;
float runS = 0;
uint32_t overloadMs = 0;

float gProg = 0;
const char *gGuide = "";

bool hasRef = false;
float eRef, tRef, vsRef, veRef, dRef;

uint32_t nextUs = 0, lastWin = 0;

// ---------- LCD ----------
void lcdLine(uint8_t row, const char *txt) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16s", txt);
  lcd.setCursor(0, row);
  lcd.print(buf);
}

void updateLCD() {
  char a[24], b[24];
  snprintf(a, sizeof(a), "I%.2fA V%.3fg", I, vib);
  if (fault)                  snprintf(b, sizeof(b), "FAULT send r");
  else if (running && hasRef) snprintf(b, sizeof(b), "%3d%% %s", (int)(gProg * 100), gGuide);
  else                        snprintf(b, sizeof(b), "D%3d%% T%2.0fC %s", (int)(duty * 100), tempC, running ? "RUN" : "IDLE");
  lcdLine(0, a);
  lcdLine(1, b);
}

// ---------- MPU6050 ----------
void mpuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

int16_t rd16() {
  uint8_t h = Wire.read();
  uint8_t l = Wire.read();
  return (int16_t)((h << 8) | l);
}

bool readAccel(float &ax, float &ay, float &az) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)6) != 6) return false;
  ax = rd16() / 8192.0f;   // +/-4 g range
  ay = rd16() / 8192.0f;
  az = rd16() / 8192.0f;
  return true;
}

float readTemp() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x41);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)2);
  return rd16() / 340.0f + 36.53f;
}

// ---------- Motor / safety ----------
void motorOff() { ledcWrite(PWM_PIN, 0); }

void tripFault(const char *why) {
  fault = true;
  running = false;
  motorOff();
  Serial.printf("### FAULT: %s - motor off. Send 'r' to reset.\n", why);
  updateLCD();
}

void zeroCurrent() {
  motorOff();
  delay(300);
  double s = 0;
  for (int i = 0; i < 500; i++) { s += analogReadMilliVolts(ACS_PIN); delay(2); }
  acsOffsetV = (s / 500.0) / 1000.0 / DIV_RATIO;
  Serial.printf("ACS712 zero offset = %.3f V (expect about 2.4-2.6)\n", acsOffsetV);
  if (acsOffsetV < 2.0f || acsOffsetV > 3.0f)
    Serial.println("WARNING: offset out of range - check ACS712 OUT, divider and GPIO 1 wiring.");
}

// ---------- Profile ----------
void loadRef() {
  hasRef = prefs.isKey("eRef");
  if (!hasRef) return;
  eRef = prefs.getFloat("eRef"); tRef = prefs.getFloat("tRef");
  vsRef = prefs.getFloat("vsRef"); veRef = prefs.getFloat("veRef");
  dRef = prefs.getFloat("dRef");
}

void printRef() {
  if (!hasRef) { Serial.println("No saved profile."); return; }
  Serial.printf("PROFILE: energy=%.5f Wh, time=%.1f s, vib start=%.4f g, vib end=%.4f g, duty=%.0f%%\n",
                eRef, tRef, vsRef, veRef, dRef * 100);
}

void saveRef() {
  if (!vibStartSet || energyWh <= 0) { Serial.println("Run for at least 1-2 s first."); return; }
  prefs.putFloat("eRef", energyWh); prefs.putFloat("tRef", runS);
  prefs.putFloat("vsRef", vibStart); prefs.putFloat("veRef", vibEma);
  prefs.putFloat("dRef", duty);
  loadRef();
  Serial.print("Saved. "); printRef();
}

void startRun() {
  if (fault) { Serial.println("Fault active - send 'r' first."); return; }
  energyWh = 0; runS = 0; vibEma = 0; vibStart = 0;
  vibStartSet = false; overloadMs = 0; running = true;
  gProg = 0; gGuide = "";
  Serial.println("t_s,duty_pct,I_A,vib_g,temp_C,E_Wh,progress,guide");
}

void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 's') startRun();
    else if (c == 'e') {
      running = false; motorOff();
      Serial.printf("RUN END: %.1f s, %.5f Wh, vib %.4f g\n", runS, energyWh, vibEma);
    }
    else if (c == 'c') saveRef();
    else if (c == 'p') printRef();
    else if (c == 'x') { prefs.clear(); hasRef = false; Serial.println("Profile cleared."); }
    else if (c == 'r') { fault = false; Serial.println("Fault cleared."); }
    else if (c == 'z' && !running) zeroCurrent();
  }
}

// ---------- Sampling ----------
void sampleOnce() {
  sumMv += analogReadMilliVolts(ACS_PIN);
  nSamp++;
  float ax, ay, az;
  if (readAccel(ax, ay, az)) {
    mx += 0.01f * (ax - mx); my += 0.01f * (ay - my); mz += 0.01f * (az - mz);
    float dx = ax - mx, dy = ay - my, dz = az - mz;
    sumSq += dx * dx + dy * dy + dz * dz;
    nAcc++;
  }
}

void processWindow() {
  if (nSamp == 0) return;
  nodeMv = sumMv / nSamp;
  float acsV = nodeMv / 1000.0f / DIV_RATIO;
  I = max(0.0f, (acsV - acsOffsetV) / ACS_SENS);
  vib = nAcc ? sqrt(sumSq / nAcc) : 0;
  sumMv = 0; sumSq = 0; nSamp = 0; nAcc = 0;
  tempC = readTemp();

  // pot maps 5%..100% -> MIN_DUTY..100% duty so the motor always starts
  long p = 0;
  for (int i = 0; i < 8; i++) p += analogRead(POT_PIN);
  float pot = (p / 8) / 4095.0f;
  if (pot < 0.05f) duty = 0;
  else             duty = MIN_DUTY + (1.0f - MIN_DUTY) * pot;

  if (running) {
    ledcWrite(PWM_PIN, (uint32_t)(duty * ((1 << PWM_BITS) - 1)));
    runS += WINDOW_MS / 1000.0f;
    energyWh += SUPPLY_V * I * (WINDOW_MS / 1000.0) / 3600.0;
    vibEma = (vibEma == 0) ? vib : vibEma + 0.2f * (vib - vibEma);
    if (!vibStartSet && runS >= 1.0f) { vibStart = vibEma; vibStartSet = true; }

    if (runS > STARTUP_GRACE) {
      overloadMs = (I > I_LIMIT_A) ? overloadMs + WINDOW_MS : 0;
      if (overloadMs >= OVERLOAD_MS) { tripFault("OVERLOAD / STALL"); return; }
    }
    if (tempC > TEMP_LIMIT_C) { tripFault("OVER-TEMPERATURE"); return; }

    float prog = 0;
    const char *guide = "no profile";
    if (hasRef) {
      float pE = energyWh / eRef;
      float pV = NAN, den = vsRef - veRef;
      if (vibStartSet && fabs(den) > 0.002f) pV = (vibStart - vibEma) / den;
      bool conf;
      if (!isnan(pV)) { prog = 0.5f * (pE + pV); conf = fabs(pE - pV) < 0.25f; }
      else            { prog = pE; conf = false; }
      if (fabs(duty - dRef) > 0.05f) conf = false;   // speed differs from reference
      if (!conf)              guide = "LOW-CONF";
      else if (prog >= 1.0f)  guide = "STOP";
      else if (prog >= 0.85f) guide = "NEAR";
      else                    guide = "CONTINUE";
    }
    gProg = prog;
    gGuide = guide;
    Serial.printf("%.1f,%.0f,%.3f,%.4f,%.1f,%.5f,%.2f,%s\n",
                  runS, duty * 100, I, vib, tempC, energyWh, prog, guide);
  } else {
    motorOff();
    static uint8_t d = 0;
    if (++d >= 5) {
      d = 0;
      Serial.printf("IDLE: ACS node=%.0f mV (expect ~1250), pot duty=%.0f%%, temp=%.1f C\n",
                    nodeMv, duty * 100, tempC);
    }
  }

  // refresh the LCD about every 0.6 s (LCD writes are slow)
  static uint8_t k = 0;
  if (++k >= 3) { k = 0; updateLCD(); }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("BOOT OK");
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);

  lcd.init();
  lcd.backlight();
  Wire.begin(SDA_PIN, SCL_PIN);   // re-assert pins after lcd.init()
  Wire.setClock(400000);
  lcdLine(0, "Kaadhu 2.0");
  lcdLine(1, "Starting...");

  mpuWrite(0x6B, 0x00);   // wake
  mpuWrite(0x1A, 0x01);   // DLPF ~184 Hz
  mpuWrite(0x1C, 0x08);   // +/-4 g
  analogReadResolution(12);
  ledcAttach(PWM_PIN, PWM_FREQ, PWM_BITS);
  motorOff();
  prefs.begin("kaadhu", false);
  loadRef();

  Wire.beginTransmission(MPU_ADDR); Wire.write(0x75); Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)1);
  Serial.printf("MPU WHO_AM_I = 0x%02X\n", Wire.read());

  zeroCurrent();
  printRef();
  Serial.println("Commands: s=start  e=end  c=save profile  p=show  x=clear  z=re-zero current  r=reset fault");
  updateLCD();
  nextUs = micros();
  lastWin = millis();
}

void loop() {
  uint32_t now = micros();
  if ((int32_t)(now - nextUs) >= 0) {
    nextUs += 1000;                                   // ~1 kHz sampling
    if ((int32_t)(now - nextUs) > 5000) nextUs = now; // resync if we fall behind
    sampleOnce();
  }
  if (millis() - lastWin >= WINDOW_MS) {
    lastWin += WINDOW_MS;
    processWindow();
  }
  handleSerial();
}
