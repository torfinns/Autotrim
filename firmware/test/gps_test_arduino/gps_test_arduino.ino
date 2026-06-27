// ============================================================================
//  Autotrim — STANDALONE GPS-TEST  (TBS M10N på UART2)   [Arduino IDE-versjon]
// ----------------------------------------------------------------------------
//  Formål: bekrefte at ESP32 får kontakt med GPS-en og mottar NMEA, FØR den
//  fulle fastvaren kjøres. Rører IKKE reléer eller BLE.
//
//  Kobling (samme som config.h / Autotrim_kobling_og_IO.md):
//     GPS TX  -> ESP GPIO16 (RX2)        <-- merk kryssing
//     GPS RX  -> ESP GPIO17 (TX2)
//     GPS VCC -> 5V   (M10N forsynes fra 5V)
//     GPS GND -> GND  (felles)
//
//  Arduino IDE:
//     1) Tools -> Board -> "ESP32 Dev Module"
//     2) Tools -> Port  -> COM-porten (CP2102)
//     3) Upload (pil), så Serial Monitor @ 115200 baud
//
//  Tre moduser via MODE under:
//     0 = AUTO+PARSE : finn baud automatisk, vis fix/sats/SOG + rå NMEA  (anbefalt først)
//     1 = RAW ECHO   : send alt fra GPS rått til USB på fast baud (FORCED_BAUD)
//     2 = BRIDGE     : transparent USB<->GPS-bro for u-center (konfig/baud-bytte)
// ============================================================================

#define MODE 0                     // 0=auto+parse, 1=raw echo, 2=bridge
static const uint32_t FORCED_BAUD = 9600;   // brukt i MODE 1 og 2

// Pinner — MÅ stemme med firmware/src/config.h
static const int PIN_GPS_RX = 16;  // ESP RX2  <- GPS TX
static const int PIN_GPS_TX = 17;  // ESP TX2  -> GPS RX

#define GPS Serial2

// Vanlige u-blox/NMEA-rater å prøve (M10 leveres ofte på 9600, config bruker 115200)
static const uint32_t CANDIDATES[] = {9600, 38400, 57600, 115200, 19200, 4800};
static const int N_CANDIDATES = sizeof(CANDIDATES) / sizeof(CANDIDATES[0]);

static uint32_t activeBaud = 0;

// ---- enkel NMEA-akkumulator (linje-for-linje) ----
static char    line[120];
static uint8_t lineLen = 0;

// telleverk for periodisk status
static uint32_t tStatus = 0;
static uint32_t sentenceCount = 0;
static char     lastFixQ = '0';     // GGA fix quality (0=ingen)
static int      lastSats = 0;
static char     lastRmcStatus = 'V';// RMC status (A=gyldig, V=ugyldig)
static float    lastSogKn = 0.0f;

// hent n-te kommaseparerte felt fra en NMEA-setning; tom streng om mangler
static String nmeaField(const char* s, int idx) {
  int field = 0;
  String out = "";
  for (const char* p = s; *p && *p != '*'; ++p) {
    if (*p == ',') { field++; continue; }
    if (field == idx) out += *p;
    if (field > idx) break;
  }
  return out;
}

static void parseLine(const char* s) {
  // s starter med '$' ... f.eks. $GNGGA / $GPGGA / $GNRMC
  if (strlen(s) < 6 || s[0] != '$') return;
  sentenceCount++;
  const char* type = s + 3;                 // hopp over "$Gx"
  if (strncmp(type, "GGA", 3) == 0) {
    String q  = nmeaField(s, 6);            // fix quality
    String nv = nmeaField(s, 7);            // antall satellitter brukt
    if (q.length())  lastFixQ = q[0];
    if (nv.length()) lastSats = nv.toInt();
  } else if (strncmp(type, "RMC", 3) == 0) {
    String st = nmeaField(s, 2);            // status A/V
    String sp = nmeaField(s, 7);            // speed over ground i knop
    if (st.length()) lastRmcStatus = st[0];
    if (sp.length()) lastSogKn = sp.toFloat();
  }
}

// Lytt en periode på gjeldende baud og vurder om vi ser gyldig NMEA.
static bool probeBaud(uint32_t baud, uint32_t ms) {
  GPS.begin(baud, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  delay(60);
  while (GPS.available()) GPS.read();        // tøm
  uint32_t t0 = millis();
  int bytes = 0, dollars = 0; bool nl = false;
  while (millis() - t0 < ms) {
    while (GPS.available()) {
      char c = (char)GPS.read();
      bytes++;
      if (c == '$') dollars++;
      if (c == '\n') nl = true;
    }
  }
  Serial.printf("  %6lu baud : %4d bytes, %2d '$', linjeskift=%s\n",
                (unsigned long)baud, bytes, dollars, nl ? "ja" : "nei");
  bool ok = (dollars >= 2 && nl);            // minst et par setninger med ramme
  if (!ok) GPS.end();
  return ok;
}

static uint32_t autodetectBaud() {
  Serial.println(F("Auto-detekterer GPS-baud (lytter ~1,2 s per rate):"));
  for (int i = 0; i < N_CANDIDATES; ++i) {
    if (probeBaud(CANDIDATES[i], 1200)) return CANDIDATES[i];
  }
  return 0;
}

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println();
  Serial.println(F("=========================================="));
  Serial.println(F(" Autotrim GPS-test (TBS M10N, UART2)"));
  Serial.println(F("------------------------------------------"));
  Serial.printf ( " Pinner: GPS TX -> GPIO%d (RX2), GPS RX -> GPIO%d (TX2)\n",
                  PIN_GPS_RX, PIN_GPS_TX);
  Serial.println(F(" GPS VCC=5V, GND=felles. Krysset TX/RX!"));
  Serial.println(F("==========================================\n"));

#if MODE == 2
  // -------- BRIDGE: transparent USB<->GPS for u-center --------
  Serial.printf("BRIDGE-modus @ %lu baud. Koble u-center til denne COM-porten.\n",
                (unsigned long)FORCED_BAUD);
  GPS.begin(FORCED_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  activeBaud = FORCED_BAUD;
#elif MODE == 1
  // -------- RAW ECHO på fast baud --------
  Serial.printf("RAW ECHO @ %lu baud.\n\n", (unsigned long)FORCED_BAUD);
  GPS.begin(FORCED_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  activeBaud = FORCED_BAUD;
#else
  // -------- AUTO + PARSE --------
  activeBaud = autodetectBaud();
  if (activeBaud == 0) {
    Serial.println(F("\n*** FANT INGEN GYLDIG NMEA PÅ NOEN RATE ***"));
    Serial.println(F("Sjekk: 1) TX/RX kryss  2) 5V+GND  3) at GPS faktisk er på"));
    Serial.println(F("       4) prøv MODE 2 (bridge) + u-center for å verifisere modulen"));
  } else {
    Serial.printf("\n>>> GPS funnet på %lu baud. Viser data...\n", (unsigned long)activeBaud);
    if (activeBaud != 115200) {
      Serial.println(F("MERK: dette er IKKE 115200 som config.h bruker."));
      Serial.println(F("      Enten endre GPS_BAUD i config.h til denne raten,"));
      Serial.println(F("      eller konfigurer modulen til 115200 (u-center / UBX-CFG-PRT)."));
    }
    Serial.println(F("(Innendørs: NMEA kommer, men fix=0/sats lavt er normalt — krever fri sikt.)\n"));
    tStatus = millis();
  }
#endif
}

void loop() {
#if MODE == 2
  // transparent bro begge veier
  while (GPS.available())    Serial.write(GPS.read());
  while (Serial.available()) GPS.write(Serial.read());
  return;
#else
  while (GPS.available()) {
    char c = (char)GPS.read();

  #if MODE == 1
    Serial.write(c);                 // rått ekko
  #else
    // samle linje, parse ved linjeslutt, og ekko rått slik at du ser setningene
    if (c == '\n' || c == '\r') {
      if (lineLen > 0) {
        line[lineLen] = '\0';
        Serial.print(line); Serial.println();
        parseLine(line);
        lineLen = 0;
      }
    } else if (lineLen < sizeof(line) - 1) {
      line[lineLen++] = c;
    }
  #endif
  }

  #if MODE == 0
  // periodisk sammendrag hvert 2. sekund
  if (millis() - tStatus >= 2000) {
    tStatus = millis();
    Serial.printf("---- STATUS @ %lu baud | setn/2s: %lu | RMC: %c | fixQ: %c | sats: %d | SOG: %.1f kn ----\n",
                  (unsigned long)activeBaud, (unsigned long)sentenceCount,
                  lastRmcStatus, lastFixQ, lastSats, lastSogKn);
    sentenceCount = 0;
  }
  #endif
#endif
}
