# Autotrim

DIY automatisk trimplan-styring for båt. En ESP32 retter opp sideveis slagside (roll) ved å styre et Lenco trimplansystem (kontrollboks 30077-001), parallellkoblet de manuelle bryterne. Roll måles med BNO055 IMU, fart med TBS M10N GPS, og fire reed-reléer (SIP-1A05) slutter Lenco-signallinjene. Konfigureres over BLE.

## Innhold

| Mappe / fil | Innhold |
|---|---|
| `Autotrim_systemarkitektur.md` | Premiss, blokkdiagram, kontrolllogikk, failsafe |
| `Autotrim_kobling_og_IO.md` | Pinnekart, spenninger, relé↔Lenco |
| `firmware/src/` | Modulær fastvare (config, params, imu, gps, relays, control, ble_iface, main) |
| `firmware/autotrim_v1/` | Hele fastvaren samlet i én Arduino-sketch (`autotrim_v1.ino`) |
| `firmware/test/` | Frittstående testsketcher (GPS, IMU, BLE, relé, samlet benk-test) |
| `app/autotrim_control.html` | Web Bluetooth-dashbord (Chrome på Android/PC) |
| `*.svg` | Koblingsskjema, relekort-detalj, layout |

## Verifisert på benk (2026-06-24)

- GPS: 38400 baud (ikke u-blox-default 115200)
- IMU-fortegn: `rollSign = -1`, `gyroSign = +1` (styrbord lav → positiv roll)
- BLE: NimBLE 2.5.0 (se merknad under)

## Bibliotek (Arduino IDE) for `autotrim_v1`

**NimBLE-Arduino 2.5.0** (ikke eldre — se under), Adafruit BNO055 (+ Adafruit Unified Sensor + BusIO). GPS parses internt — ingen TinyGPSPlus.

### NimBLE-versjon — viktig

NimBLE-Arduino < 2.3.8 krasjer med `assert(mu->handle)` i `npl_os_freertos.c` på esp32-kjerne 3.3.7 og nyere. Bruk **2.5.0** (inkludert i `firmware/test/libraries/NimBLE-Arduino/`).

Installer i Arduino IDE: kopier `firmware/test/libraries/NimBLE-Arduino/` til `Documents/Arduino/libraries/NimBLE-Arduino/`. Pass på at `exp_nimble_mem.c` i `src/nimble/esp_port/port/src/` er tom (den er en duplikat av `esp_nimble_mem.c` og gir linkerfeil).

Fastvaren har også innebygget NVS-recovery: oppdager krasj ved omstart (`ESP_RST_PANIC`), sletter NVS og starter om — selvhelende ved stale BLE-flash-data.

## Flash / opplasting

```
# Finn arduino-cli (følger med Arduino IDE 2.x)
$cli = "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"

# Kompiler
& $cli compile --fqbn esp32:esp32:esp32 firmware\autotrim_v1

# Flash (lukk Serial Monitor først)
& $cli upload --fqbn esp32:esp32:esp32 --port COM10 firmware\autotrim_v1

# Ved hardnakket NVS-krasj: erase flash først
& "C:\Users\torfi\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\5.3.0\esptool.exe" --chip esp32 --port COM10 erase-flash
```

## Dashbord (GitHub Pages)

`app/autotrim_control.html` hostes på GitHub Pages (krever HTTPS for Web Bluetooth på mobil). Åpnes i Chrome, kobler til BLE-enheten «Autotrim».

**Knapper:**
- **Autotrim: PÅ/AV** — toggle for autoEnabled (grønn = aktiv)
- **Debug: PÅ/AV** — kobler ut farts- og GPS-krav (oransje = aktiv); slås alltid av ved omstart
- **Venstre/Høyre opp/ned** — manuell relé-puls (≈1,5 s); aktiveres kun når Debug er på
