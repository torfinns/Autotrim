#include "ble_iface.h"
#include "config.h"
#include <NimBLEDevice.h>

namespace {
  NimBLECharacteristic *chParams = nullptr;
  NimBLECharacteristic *chTele   = nullptr;
  NimBLECharacteristic *chCmd    = nullptr;
  AutotrimParams       *gParams  = nullptr;

  volatile bool    gParamsDirty = false;
  volatile uint8_t gLastCmd     = CMD_NONE;
  volatile bool    gConnected   = false;

  // NimBLE-Arduino 2.x: callbacks tar NimBLEConnInfo (og onDisconnect en reason).
  class ServerCB : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *s, NimBLEConnInfo &info) override    { gConnected = true; }
    void onDisconnect(NimBLEServer *s, NimBLEConnInfo &info, int reason) override { gConnected = false; NimBLEDevice::startAdvertising(); }
  };

  class ParamsCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &info) override {
      NimBLEAttValue v = c->getValue();
      if (v.length() == sizeof(AutotrimParams) && gParams) {
        AutotrimParams tmp;
        memcpy(&tmp, v.data(), sizeof(tmp));
        if (params_valid(tmp)) {
          *gParams = tmp;          // main lagrer til NVS når dirty konsumeres
          gParamsDirty = true;
        }
      }
    }
  };

  class CmdCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &info) override {
      NimBLEAttValue v = c->getValue();
      if (v.length() >= 1) gLastCmd = (uint8_t)v[0];
    }
  };

  ServerCB serverCB;
  ParamsCB paramsCB;
  CmdCB    cmdCB;
}

void BleIface::begin(AutotrimParams *params) {
  gParams = params;

  NimBLEDevice::init(BLE_DEVICE_NAME);
  NimBLEDevice::setPower(9);   // dBm (NimBLE 2.x-signatur; maks rekkevidde)

  NimBLEServer *server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCB);

  NimBLEService *svc = server->createService(UUID_SVC);

  chParams = svc->createCharacteristic(
      UUID_CHAR_PARAMS, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
  chParams->setCallbacks(&paramsCB);
  chParams->setValue((uint8_t*)gParams, sizeof(AutotrimParams));

  chTele = svc->createCharacteristic(
      UUID_CHAR_TELEMETRY, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

  chCmd = svc->createCharacteristic(
      UUID_CHAR_COMMAND, NIMBLE_PROPERTY::WRITE);
  chCmd->setCallbacks(&cmdCB);

  svc->start();

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(UUID_SVC);
  adv->enableScanResponse(true);
  NimBLEDevice::startAdvertising();
}

void BleIface::publishTelemetry(const Telemetry &t) {
  if (!chTele) return;
  chTele->setValue((uint8_t*)&t, sizeof(t));
  if (gConnected) chTele->notify();
}

bool BleIface::consumeParamsDirty() {
  if (gParamsDirty) { gParamsDirty = false; return true; }
  return false;
}

Command BleIface::takeCommand() {
  uint8_t c = gLastCmd;
  gLastCmd = CMD_NONE;
  return (Command)c;
}

bool BleIface::connected() const { return gConnected; }
