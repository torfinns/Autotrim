// ble_iface.h — NimBLE GATT: parametere (RW), telemetri (notify), kommando (W)
#pragma once
#include <Arduino.h>
#include "params.h"
#include "types.h"

class BleIface {
public:
  void begin(AutotrimParams *params);     // params eies av main; skrives ved BLE-write
  void publishTelemetry(const Telemetry &t);

  bool    consumeParamsDirty();            // true én gang etter at app skrev params
  Command takeCommand();                   // henter siste kommando (CMD_NONE hvis ingen)
  bool    connected() const;
};
