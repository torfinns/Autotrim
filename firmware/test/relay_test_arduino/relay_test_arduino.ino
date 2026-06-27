// ============================================================================
//  Autotrim — STANDALONE RELÉ-TEST   [Arduino IDE]
// ----------------------------------------------------------------------------
//  Tester de 4 reed-reléene (SIP-1A05, direkte fra 3,3V GPIO) ENKELTVIS, med
//  sikkerhet i høysetet:
//     - Alle reléer AV som aller første handling ved boot.
//     - Kun ÉN kanal på om gangen (interlock — aldri U+D samme side).
//     - Kort puls (PULSE_MS) + HARD maks-tid (MAX_ON_MS) mot hengende relé.
//     - Du trigger hver kanal manuelt fra Serial Monitor.
//
//  ADVARSEL: Hvis relékontaktene er koblet i parallell med Lenco-bryterne og
//  planene henger på, vil HVER puls bevege et trimplan. Test gjerne FØRST uten
//  Lenco og mål kontakt-slutning med multimeter (jf. Autotrim_kobling_og_IO §7).
//
//  Pinner (config.h): LU=33, LD=25, RU=26, RD=27. Aktiv-høy modul.
//  Board = "ESP32 Dev Module", Serial Monitor @ 115200 (sett linjeslutt "Newline").
//
//  KOMMANDOER (skriv i Serial Monitor + Enter):
//     1 = LU (venstre opp)   2 = LD (venstre ned)
//     3 = RU (høyre opp)     4 = RD (høyre ned)
//     0 = alle av            h = hjelp
// ============================================================================
#include <Arduino.h>

static const bool RELAY_ACTIVE_HIGH = true;        // MÅ stemme med modulen/config.h
static const int  relPins[4] = {33, 25, 26, 27};   // LU, LD, RU, RD
static const char *relName[4] = {"LU (venstre opp)", "LD (venstre ned)",
                                 "RU (høyre opp)",   "RD (høyre ned)"};

static const uint32_t PULSE_MS  = 1000;            // hvor lenge en kanal holdes på per kommando
static const uint32_t MAX_ON_MS = 2000;            // HARD grense (hengende relé) — failsafe
static const uint32_t STARTUP_UP_MS = 3000;        // boot: kjør begge plan OPP (kjent nøytral)

static int      activeCh = -1;                     // -1 = ingen på (interlock)
static uint32_t onSince  = 0;

static void relWrite(int idx, bool on) {
  digitalWrite(relPins[idx], (on == RELAY_ACTIVE_HIGH) ? HIGH : LOW);
}
static void allOff() {
  for (int i = 0; i < 4; i++) relWrite(i, false);
  activeCh = -1;
}
static void bootHomeUp() {
  // BOOT: begge plan OPP (LU + RU) i STARTUP_UP_MS -> kjent nøytral (helt oppe)
  // når vi ikke har sensorikk. LU+RU er begge OPP -> bryter ikke interlock.
  Serial.printf("BOOT: kjorer BEGGE plan OPP i %lu ms...\n", (unsigned long)STARTUP_UP_MS);
  relWrite(0, true); relWrite(2, true);            // LU + RU
  delay(STARTUP_UP_MS);
  allOff();
  Serial.println(F("BOOT: ferdig - begge plan skal sta helt oppe. Reler AV."));
}
static void help() {
  Serial.println(F("\nKommandoer:  1=LU  2=LD  3=RU  4=RD  0=alle av  h=hjelp"));
  Serial.printf ("Puls: %lums, hard maks: %lums. Kun én kanal om gangen.\n",
                 (unsigned long)PULSE_MS, (unsigned long)MAX_ON_MS);
}

void setup() {
  // SIKKERHET: alle reléer AV aller først, før noe annet.
  for (int i = 0; i < 4; i++) { pinMode(relPins[i], OUTPUT); relWrite(i, false); }

  Serial.begin(115200);
  delay(400);
  Serial.println(F("\n=== Autotrim RELÉ-TEST ==="));
  Serial.println(F("Alle reléer satt AV ved boot. Aktiv-høy modus."));
  Serial.println(F("ADVARSEL: hvis Lenco/planene er koblet til, beveger hver puls et plan!"));
  bootHomeUp();                                    // default: begge opp 3 s ved oppstart
  help();
  Serial.println(F("Hviletilstand: alle reler AV -> manuelle Lenco-knapper virker (parallell)."));
}

void loop() {
  // --- les kommando ---
  if (Serial.available()) {
    int c = Serial.read();
    if (c >= '1' && c <= '4') {
      allOff();                              // interlock: slå av alt før ny kanal
      int idx = c - '1';
      relWrite(idx, true); activeCh = idx; onSince = millis();
      Serial.printf("PÅ: K%d %s  (auto-av om %lu ms)\n", idx + 1, relName[idx], (unsigned long)PULSE_MS);
    } else if (c == '0') {
      allOff(); Serial.println(F("ALLE AV"));
    } else if (c == 'h' || c == 'H') {
      help();
    }
    // andre tegn (linjeslutt osv.) ignoreres
  }

  // --- auto-av: puls ferdig, eller hard grense ---
  if (activeCh >= 0) {
    uint32_t on = millis() - onSince;
    if (on >= MAX_ON_MS) {
      allOff(); Serial.println(F("AV (HARD GRENSE nådd!)"));
    } else if (on >= PULSE_MS) {
      allOff(); Serial.println(F("AV (puls ferdig)"));
    }
  }
}
