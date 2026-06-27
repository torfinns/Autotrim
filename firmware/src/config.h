// config.h — pinner, faste konstanter og BLE-UUID-er for Autotrim
#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// PINNER  (se Autotrim_kobling_og_IO.md)
// ---------------------------------------------------------------------------
// I2C (BNO055)
static const int PIN_I2C_SDA = 21;
static const int PIN_I2C_SCL = 22;
static const int PIN_BNO_RST = 4;     // valgfri; sett -1 om ikke koblet

// UART2 (TBS M10N GPS).  GPS TX -> ESP RX, GPS RX -> ESP TX
static const int PIN_GPS_RX  = 16;    // ESP RX2  <- GPS TX
static const int PIN_GPS_TX  = 17;    // ESP TX2  -> GPS RX
static const uint32_t GPS_BAUD = 38400;   // verifisert mot modulen (bench-test 2026-06-24)

// Reléer (fortløpende på venstre header, RTC, ingen boot-glitch/strapping)
static const int PIN_REL_LU = 33;     // venstre opp
static const int PIN_REL_LD = 25;     // venstre ned
static const int PIN_REL_RU = 26;     // høyre opp
static const int PIN_REL_RD = 27;     // høyre ned

// Relémodulens trigger-polaritet. true = aktiv-høy (IN=HIGH -> relé på).
// VERIFISER fysisk før Lenco kobles til (se IO-doc §5).
static const bool RELAY_ACTIVE_HIGH = true;

// ---------------------------------------------------------------------------
// LØKKE-RATER
// ---------------------------------------------------------------------------
static const uint32_t IMU_PERIOD_MS   = 10;    // 100 Hz sensorfusjon
static const uint32_t CTRL_PERIOD_MS  = 50;    // 20 Hz kontroll/relé
static const uint32_t TELE_PERIOD_MS  = 200;   // 5 Hz BLE-telemetri

// Oppstartsrutine
static const uint32_t STARTUP_UP_MS   = 3000;  // kjør begge opp ved boot

// ---------------------------------------------------------------------------
// BLE UUID-er  (egendefinerte 128-bit; app må bruke de samme)
// ---------------------------------------------------------------------------
#define BLE_DEVICE_NAME      "Autotrim"
#define UUID_SVC             "9a8b0001-7b2c-4f3d-9e6a-2b1c3d4e5f60"
#define UUID_CHAR_PARAMS     "9a8b0002-7b2c-4f3d-9e6a-2b1c3d4e5f60" // read/write (struct)
#define UUID_CHAR_TELEMETRY  "9a8b0003-7b2c-4f3d-9e6a-2b1c3d4e5f60" // read/notify (struct)
#define UUID_CHAR_COMMAND    "9a8b0004-7b2c-4f3d-9e6a-2b1c3d4e5f60" // write (1 byte kommando)
