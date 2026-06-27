# Autotrim

DIY automatisk trimplan-styring for båt. En ESP32 retter opp sideveis slagside (roll) ved å styre et Lenco trimplansystem (kontrollboks 30077-001), parallellkoblet de manuelle bryterne. Roll måles med BNO055 IMU, fart med TBS M10N GPS, og fire reed-reléer (SIP-1A05) slutter Lenco-signallinjene. Konfigureres over BLE.

## Innhold

| Mappe / fil | Innhold |
|---|---|
| `Autotrim_systemarkitektur.md` | Premiss, blokkdiagram, kontrolllogikk, failsafe |
| `Autotrim_kobling_og_IO.md` | Pinnekart, spenninger, relé↔Lenco |
| `firmware/autotrim_v1/` | Fastvare i én Arduino-sketch (`autotrim_v1.ino`) |
| `app/autotrim_control.html` | Web Bluetooth-dashbord (Chrome på Android/PC) |
| `*.svg` | Koblingsskjema, relekort-detalj, layout |

## Kontrollarkitektur

**Diskret integrasjon med pulse-og-vent:**
- Regulatoren evaluerer feilen én gang per syklus (etter at relé er ferdig + 1,5 s settle-tid)
- Innenfor dødbåndet: ingen handling — plan holdes i nåværende posisjon
- Utenfor dødbåndet: `_trimFrac += kP × feil × 0,5 s` → beregner ny planposisjon → kjører relé
- Anti-windup: ved retningsskifte nullstilles `_trimFrac` slik at korreksjonen starter fra nøytral
- Sekvensering: motparten trekkes alltid opp til < 10 % av slaglengde før den aktive siden kjøres ned
- Minimum pådragstid: 500 ms (hold-timer per retning)

**Parametere som faktisk brukes:**
- `kP` (GUI viser ×100): proporsjonalforsterkning — typisk 10–20
- `rollDeadbandDeg`: ingen korrigering innenfor ±X grader
- `fusionAlpha`: komplementærfilter gyro vs. akselerometer
- `fullStrokeMs`, `maxDeployFrac`, `neutralFrac`: mekaniske grenser
- `speedOnKn` / `speedOffKn`: fartslås med hysterese
- `rollSetpointDeg`, `mountingOffsetDeg`, `rollSign`: kalibrering

**Parametere i struct men ikke i bruk (GUI skjult):** `kI`, `cmdTauSec`, `gyroSign`

## Verifisert på benk (2026-06-27)

- GPS: 38400 baud (ikke u-blox-default 115200)
- IMU-fortegn: `rollSign = +1` (verifiser at styrbord lav → positiv roll på skjermen)
- `gyroSign`-feltet ignoreres — `rollSign` brukes for begge akser
- `PIN_BNO_RST = -1` — RST på BNO055 er ikke koblet til ESP32; -1 hopper over reset-sekvensen (BNO055 bruker intern POR)
- BLE: NimBLE 2.5.0 (se merknad under)

## Bibliotek (Arduino IDE) for `autotrim_v1`

**NimBLE-Arduino 2.5.0** (ikke eldre — se under), Adafruit BNO055 (+ Adafruit Unified Sensor + BusIO). GPS parses internt — ingen TinyGPSPlus.

### NimBLE-versjon — viktig

NimBLE-Arduino < 2.3.8 krasjer med `assert(mu->handle)` i `npl_os_freertos.c` på esp32-kjerne 3.3.7 og nyere. Bruk **2.5.0** (inkludert i `firmware/test/libraries/NimBLE-Arduino/`).

Installer i Arduino IDE: kopier `firmware/test/libraries/NimBLE-Arduino/` til `Documents/Arduino/libraries/NimBLE-Arduino/`. Pass på at `exp_nimble_mem.c` i `src/nimble/esp_port/port/src/` er tom (den er en duplikat av `esp_nimble_mem.c` og gir linkerfeil).

Fastvaren har også innebygget NVS-recovery: oppdager krasj ved omstart (`ESP_RST_PANIC`), sletter NVS og starter om — selvhelende ved stale BLE-flash-data.

## Flash / opplasting

```powershell
# Finn arduino-cli (følger med Arduino IDE 2.x)
$cli = "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"

# Kompiler
& $cli compile --fqbn esp32:esp32:esp32 firmware\autotrim_v1

# Flash (lukk Serial Monitor først — COM10 blir opptatt)
& $cli upload --fqbn esp32:esp32:esp32 --port COM10 firmware\autotrim_v1

# Ved hardnakket NVS-krasj: erase flash først
& "C:\Users\torfi\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\5.3.0\esptool.exe" --chip esp32 --port COM10 erase-flash
```

## Dashbord (GitHub Pages)

`app/autotrim_control.html` hostes på GitHub Pages (krever HTTPS for Web Bluetooth på mobil). Åpnes i Chrome, kobler til BLE-enheten «Autotrim».

**Knapper og funksjoner:**
- **Autotrim: PÅ/AV** — toggle for autoEnabled (grønn = aktiv)
- **Debug: PÅ/AV** — kobler ut farts- og GPS-krav for benk-test (oransje = aktiv); slås alltid av ved omstart
- **LEFT/RIGHT UP/DOWN** — manuell relé-puls (≈1,5 s); aktiveres kun når Debug er på; lyser grønt ved aktivt relé
- **NEUTRAL / HOME** — kjører begge plan opp
- **Gjenopprett anbefalte verdier** — tre preset-sett (Fabrikk/Sport/Glatt); laster inn i feltene uten å sende

## AutotrimParams struct (56 bytes, PARAMS_VERSION=2)

| Offset | Felt | Type | Merknad |
|---|---|---|---|
| 0 | magic | uint16 | 0xA770 |
| 2 | version | uint8 | **= 2** — firmware avviser stille ved mismatch |
| 3 | autoEnabled | uint8 | |
| 4–44 | speedOnKn … neutralFrac | float×11 | |
| 48 | rollSign | int8 | brukes for begge akser (accel + gyro) |
| 49 | gyroSign | int8 | ignoreres i firmware |
| 50 | testBypass | uint8 | Debug-flagg |
| 51 | (reserved) | uint8 | |
| 52 | mountingOffsetDeg | float | Kompenserer for skjev sensormontering |

> **OBS:** Når struct utvides og PARAMS_VERSION bumpes i firmware, må `dv.setUint8(2, <ny versjon>)` i GUI `buildParams()` oppdateres tilsvarende.
