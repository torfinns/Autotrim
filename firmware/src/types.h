// types.h — delte typer (tilstand, side, telemetri)
#pragma once
#include <Arduino.h>

enum class State : uint8_t { BOOT_UP = 0, STANDBY = 1, ACTIVE = 2, FAULT = 3 };
enum class Side  : int8_t  { NONE = 0, LEFT = 1, RIGHT = 2 };

// Feilflagg (bitmaske)
enum FaultBits : uint16_t {
  FAULT_IMU      = 1 << 0,
  FAULT_GPS_FIX  = 1 << 1,
  FAULT_IMU_SANITY = 1 << 2,
};

// Pakket telemetri => sendes rått over BLE (notify). Se README for layout.
#pragma pack(push, 1)
struct Telemetry {
  uint32_t millisUp;     // oppetid ms
  float    rollDeg;      // filtrert rollvinkel
  float    rollRateDps;  // gyro-Z rate
  float    accX;         // rå accel (debug)
  float    accY;
  float    sogKn;        // fart over grunn
  uint8_t  gpsFix;       // 0/1
  uint8_t  sats;         // antall satellitter
  uint8_t  state;        // State
  int8_t   activeSide;   // Side
  float    posLeftFrac;  // estimert deploy 0..1 (venstre)
  float    posRightFrac; // (høyre)
  uint8_t  relayBits;    // bit0 LU, bit1 LD, bit2 RU, bit3 RD
  uint16_t faults;       // FaultBits
};
#pragma pack(pop)

// Kommandoer (UUID_CHAR_COMMAND, 1 byte)
enum Command : uint8_t {
  CMD_NONE        = 0,
  CMD_BOTH_UP     = 1,   // kjør begge helt opp nå
  CMD_RECAL_UP    = 2,   // re-referer: kjør begge opp lenger enn full slaglengde
  CMD_SAVE_PARAMS = 3,   // lagre gjeldende parametere til NVS
  CMD_SET_ROLL_ZERO = 4, // nullstill rollvinkel-offset (i ro, vater)
};
