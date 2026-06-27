// ============================================================================
//  Autotrim — SAMLET BENK-TEST  (GPS + IMU samtidig)   [Arduino IDE]
// ----------------------------------------------------------------------------
//  Tester BÅDE TBS M10N (GPS, UART2) og BNO055 (IMU, I2C) i én sketch.
//  De deler ingen pinner, så begge kan kjøre samtidig — som i fastvaren.
//  Rører IKKE reléer eller BLE.
//
//  Slå av en av dem ved behov med flaggene under.
//
//  Kobling (samme som config.h / Autotrim_kobling_og_IO.md):
//     GPS  TX -> GPIO16 (RX2)   GPS  RX -> GPIO17 (TX2)   GPS VCC=5V  GND
//     BNO  SDA-> GPIO21         BNO  SCL-> GPIO22         BNO Vin=3V3 GND
//     BNO  RST-> GPIO4 (valgfri)   ADR/PS0/PS1 -> GND (0x28)
//
//  KREVER BIBLIOTEK (Tools -> Manage Libraries): "Adafruit BNO055"
//     (drar med "Adafruit Unified Sensor" + "Adafruit BusIO" -> Install all)
//
//  Board = "ESP32 Dev Module", Port = CP2102, Serial Monitor @ 115200.
// ============================================================================
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>

#define TEST_GPS 1            // 1 = test GPS,  0 = hopp over
#define TEST_IMU 1            // 1 = test IMU,  0 = hopp over

// ---- Pinner (MÅ stemme med firmware/src/config.h) ----
static const int PIN_GPS_RX = 16, PIN_GPS_TX = 17;
static const uint32_t GPS_BAUD = 38400;          // verifisert på modulen
static const int PIN_I2C_SDA = 21, PIN_I2C_SCL = 22, PIN_BNO_RST = 4;
static const uint8_t BNO_ADDR = 0x28;
static const float ROLL_SIGN = 1.0f, GYRO_SIGN = 1.0f;

#define GPS Serial2
Adafruit_BNO055 bno(55, BNO_ADDR, &Wire);

// ---- GPS-tilstand ----
static char    line[120]; static uint8_t lineLen = 0;
static char    rmcStatus = 'V'; static char fixQ = '0';
static int     sats = 0; static float sogKn = 0.0f;
static uint32_t gpsBytes = 0;

// ---- IMU-tilstand ----
static bool imuOk = false;

static uint32_t tPrint = 0;

static void i2cScan() {
  Serial.println(F("I2C-skann:"));
  int found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  enhet paa 0x%02X%s\n", a, (a == BNO_ADDR) ? "  <- BNO055" : "");
      found++;
    }
  }
  if (found == 0) Serial.println(F("  INGEN I2C-enheter! Sjekk Vin=3V3, SDA/SCL, ADR->GND."));
}

static String nmeaField(const char* s, int idx) {
  int f = 0; String out = "";
  for (const char* p = s; *p && *p != '*'; ++p) {
    if (*p == ',') { f++; continue; }
    if (f == idx) out += *p; if (f > idx) break;
  }
  return out;
}
static void parseLine(const char* s) {
  if (strlen(s) < 6 || s[0] != '$') return;
  const char* t = s + 3;
  if (!strncmp(t, "GGA", 3)) {
    String q = nmeaField(s, 6), n = nmeaField(s, 7);
    if (q.length()) fixQ = q[0]; if (n.length()) sats = n.toInt();
  } else if (!strncmp(t, "RMC", 3)) {
    String st = nmeaField(s, 2), sp = nmeaField(s, 7);
    if (st.length()) rmcStatus = st[0]; if (sp.length()) sogKn = sp.toFloat();
  }
}

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println(F("\n==== Autotrim benk-test (GPS + IMU) ===="));

#if TEST_GPS
  GPS.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  Serial.printf("GPS: UART2 @ %lu baud (RX=GPIO%d, TX=GPIO%d)\n",
                (unsigned long)GPS_BAUD, PIN_GPS_RX, PIN_GPS_TX);
#endif

#if TEST_IMU
  if (PIN_BNO_RST >= 0) {
    pinMode(PIN_BNO_RST, OUTPUT);
    digitalWrite(PIN_BNO_RST, LOW); delay(5); digitalWrite(PIN_BNO_RST, HIGH); delay(30);
  }
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);
  i2cScan();
  if (bno.begin(OPERATION_MODE_ACCGYRO)) {
    delay(20); bno.setExtCrystalUse(true); imuOk = true;
    Serial.println(F("IMU: BNO055 OK paa 0x28 (ACCGYRO)"));
    Serial.println(F("     Fortegn: styrbord synker -> roll POSITIV; babord -> NEGATIV."));
  } else {
    Serial.println(F("IMU: *** FANT IKKE BNO055 *** (sjekk 3V3, SDA/SCL, ADR->GND)"));
  }
#endif

  Serial.println(F("----------------------------------------"));
  Serial.println(F("(Innendoers: GPS fix=0/sats=0 er normalt. Vipp kortet for roll-sjekk.)\n"));
  tPrint = millis();
}

void loop() {
#if TEST_GPS
  while (GPS.available()) {
    char c = (char)GPS.read(); gpsBytes++;
    if (c == '\n' || c == '\r') {
      if (lineLen > 0) { line[lineLen] = '\0'; parseLine(line); lineLen = 0; }
    } else if (lineLen < sizeof(line) - 1) {
      line[lineLen++] = c;
    }
  }
#endif

  if (millis() - tPrint >= 500) {     // 2 Hz samlet status
    tPrint = millis();

#if TEST_GPS
    bool gpsAlive = gpsBytes > 0;
    Serial.printf("GPS %s | RMC:%c fixQ:%c sats:%2d SOG:%4.1f kn   ",
                  gpsAlive ? "OK " : "-- ", rmcStatus, fixQ, sats, sogKn);
    gpsBytes = 0;
#endif

#if TEST_IMU
    if (imuOk) {
      imu::Vector<3> a = bno.getVector(Adafruit_BNO055::VECTOR_ACCELEROMETER);
      imu::Vector<3> g = bno.getVector(Adafruit_BNO055::VECTOR_GYROSCOPE);
      float aX = a.x(), aY = a.y(), aZ = a.z();
      float amag = sqrtf(aX*aX + aY*aY + aZ*aZ);
      float roll = atan2f(aY, aX) * 57.2957795f * ROLL_SIGN;
      float rate = g.z() * GYRO_SIGN;
      Serial.printf("| IMU roll:%+6.1f deg  rate:%+6.1f dps  |a|:%4.1f %s",
                    roll, rate, amag, (amag > 4 && amag < 25) ? "" : "<-USUNN");
    } else {
      Serial.print("| IMU --");
    }
#endif
    Serial.println();
  }
}
