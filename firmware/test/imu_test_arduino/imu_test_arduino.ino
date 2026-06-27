// ============================================================================
//  Autotrim — STANDALONE IMU-TEST  (BNO055 på I2C)   [Arduino IDE-versjon]
// ----------------------------------------------------------------------------
//  Formål: bekrefte at ESP32 får kontakt med BNO055, og at rollvinkel + fortegn
//  stemmer med monteringen — FØR den fulle fastvaren kjøres. Rører IKKE reléer.
//
//  Speiler fastvaren (firmware/src/imu.cpp):
//     - ACCGYRO-modus (rå accel + gyro, ingen innebygd fusjon)
//     - montering: X opp, Y på tvers, Z langskips (kortet står LODDRETT)
//     - roll  = atan2(aY, aX) * 180/pi      (rollSign = +1)
//     - rate  = gyro Z                       (gyroSign = +1)
//
//  Kobling (samme som config.h / Autotrim_kobling_og_IO.md):
//     BNO055 Vin -> 3V3      (IKKE 5V)
//     BNO055 GND -> GND
//     BNO055 SDA -> GPIO21
//     BNO055 SCL -> GPIO22
//     BNO055 RST -> GPIO4    (valgfri; sett PIN_BNO_RST = -1 om ikke koblet)
//     ADR/PS0/PS1 -> GND     (adresse 0x28, I2C-modus)
//
//  KREVER BIBLIOTEK (Arduino IDE -> Tools -> Manage Libraries):
//     "Adafruit BNO055"  (drar med seg "Adafruit Unified Sensor" + "Adafruit BusIO")
//
//  Arduino IDE: Board = "ESP32 Dev Module", Port = CP2102, Upload, Serial Monitor @ 115200.
// ============================================================================
#include <Wire.h>

#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>

// Pinner — MÅ stemme med firmware/src/config.h
static const int PIN_I2C_SDA = 21;
static const int PIN_I2C_SCL = 22;
static const int PIN_BNO_RST = 4;     // sett -1 om RST ikke er koblet
static const uint8_t BNO_ADDR = 0x28;

// Fortegn (samme defaults som params): +1/+1
static const float ROLL_SIGN = 1.0f;
static const float GYRO_SIGN = 1.0f;

Adafruit_BNO055 bno(55, BNO_ADDR, &Wire);

static bool   imuOk = false;
static uint32_t tPrint = 0;

static void i2cScan() {
  Serial.println(F("I2C-skann:"));
  int found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  enhet funnet på 0x%02X%s\n", a,
                    (a == BNO_ADDR) ? "  <- BNO055" : "");
      found++;
    }
  }
  if (found == 0) {
    Serial.println(F("  INGEN I2C-enheter! Sjekk SDA/SCL/3V3/GND og pull-ups."));
  }
}

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println();
  Serial.println(F("=========================================="));
  Serial.println(F(" Autotrim IMU-test (BNO055, I2C @ 0x28)"));
  Serial.println(F("------------------------------------------"));
  Serial.printf ( " SDA=GPIO%d  SCL=GPIO%d  RST=%d\n", PIN_I2C_SDA, PIN_I2C_SCL, PIN_BNO_RST);
  Serial.println(F(" Vin=3V3 (IKKE 5V), GND felles."));
  Serial.println(F("==========================================\n"));

  if (PIN_BNO_RST >= 0) {
    pinMode(PIN_BNO_RST, OUTPUT);
    digitalWrite(PIN_BNO_RST, LOW);  delay(5);
    digitalWrite(PIN_BNO_RST, HIGH); delay(30);
  }

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);

  i2cScan();

  // ACCGYRO-modus — samme som fastvaren
  if (!bno.begin(OPERATION_MODE_ACCGYRO)) {
    Serial.println(F("\n*** FANT IKKE BNO055 ***"));
    Serial.println(F("Sjekk: 1) 0x28 i skannen over  2) Vin=3V3  3) SDA/SCL ikke byttet"));
    Serial.println(F("       4) ADR/PS0/PS1 til GND  5) I2C pull-ups (4,7k) til 3V3"));
    imuOk = false;
    return;
  }
  delay(20);
  bno.setExtCrystalUse(true);
  imuOk = true;

  Serial.println(F("\n>>> BNO055 OK. Leser accel + gyro...\n"));
  Serial.println(F("FORTEGN-SJEKK (kort loddrett, X opp, Y på tvers):"));
  Serial.println(F("  Vipp som om STYRBORD synker  -> roll skal bli POSITIV (+)"));
  Serial.println(F("  Vipp som om BABORD synker    -> roll skal bli NEGATIV (-)"));
  Serial.println(F("  Hvis motsatt: sett rollSign = -1 i params.\n"));
  Serial.println(F("I ro skal |accel| ~ 9,8 m/s^2 og gyroZ ~ 0 dps.\n"));
  tPrint = millis();
}

void loop() {
  if (!imuOk) { delay(1000); return; }

  imu::Vector<3> a = bno.getVector(Adafruit_BNO055::VECTOR_ACCELEROMETER); // m/s^2
  imu::Vector<3> g = bno.getVector(Adafruit_BNO055::VECTOR_GYROSCOPE);     // dps

  float aX = a.x(), aY = a.y(), aZ = a.z();
  float amag = sqrtf(aX*aX + aY*aY + aZ*aZ);
  bool  sane = (amag > 4.0f && amag < 25.0f) && !isnan(amag);

  float roll = atan2f(aY, aX) * 57.2957795f * ROLL_SIGN;   // grader
  float rate = g.z() * GYRO_SIGN;                          // dps

  if (millis() - tPrint >= 200) {                          // 5 Hz utskrift
    tPrint = millis();
    Serial.printf("roll: %+6.1f deg | rollrate: %+6.1f dps | aX=%+5.2f aY=%+5.2f aZ=%+5.2f | |a|=%4.1f %s\n",
                  roll, rate, aX, aY, aZ, amag, sane ? "" : "<- USUNN");
  }
}
