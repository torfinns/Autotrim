# Autotrim

DIY automatisk trimplan-styring for båt. En ESP32 retter opp sideveis slagside (roll) ved å styre et Lenco trimplansystem (kontrollboks 30077-001), parallellkoblet de manuelle bryterne. Roll måles med BNO055 IMU, fart med TBS M10N GPS, og fire reed-reléer (SIP-1A05) slutter Lenco-signallinjene. Konfigureres over BLE.

## Innhold

| Mappe / fil | Innhold |
|---|---|
| `Autotrim_systemarkitektur.md` | Premiss, blokkdiagram, kontrolllogikk, failsafe |
| `Autotrim_kobling_og_IO.md` | Pinnekart, spenninger, relé↔Lenco |
| `firmware/autotrim_v1/` | Fastvare i én Arduino-sketch (`autotrim_v1.ino`) |
| `autotrim_control.html` | Web Bluetooth-dashbord (Chrome på Android/PC) — hostes på GitHub Pages |
| `*.svg` | Koblingsskjema, relekort-detalj, layout |

## Pinnekart — reléer

| Signal | GPIO | Merknad |
|---|---|---|
| Babord opp (LU) | 25 | verifisert 2026-07-18 |
| Babord ned (LD) | 33 | |
| Styrbord opp (RU) | 27 | |
| Styrbord ned (RD) | 26 | |

> GPIO25/27 trekker plan OPP, GPIO33/26 dypper plan NED — verifisert på benk. Pins ble byttet ift. opprinnelig kobling (33↔25 og 26↔27) fordi OPP-relé fysisk dyppa planet.

## Lederfarger — Lenco-kontrollboks (30077-001)

To kabelsegmenter med ulik farge på samme signal:

| Funksjon | Panel-side | Skjøtekabel (båt → boks) |
|---|---|---|
| 12 V | svart | grå |
| Babord opp (LU) | hvit | hvit |
| Babord ned (LD) | rød | rød |
| Styrbord opp (RU) | brun | orange |
| Styrbord ned (RD) | grønn | grønn |

Kun **RU** (brun ↔ orange) og **12 V** (svart ↔ grå) skifter farge mellom segmentene.

## Kontrollarkitektur

**Diskret integrasjon med pulse-og-vent:**
- Regulatoren evaluerer feilen én gang per syklus (etter at relé er ferdig + 1,5 s settle-tid)
- Innenfor dødbåndet: ingen handling — plan holdes i nåværende posisjon
- Utenfor dødbåndet: `_trimFrac += kP × feil × 0,5 s` → beregner ny planposisjon → kjører relé
- Proporsjonal puls: `constrain(posisjonsavvik, 100 ms, 4000 ms)` — 100 ms < POS_DEADBAND_MS (120 ms) garanterer at overshoot absorberes uten ny korreksjon
- Ingen anti-windup: position-deadbåndet absorberer støy på deadband-grensen
- Sekvensering: motparten trekkes alltid opp til < 10 % av slaglengde før den aktive siden kjøres ned

**Sving-release (revidert etter sjøtest 2026-07-18):**
- Detektering krever **BÅDE** reell yaw-rate over et absolutt gulv **OG** lateralakselerasjon `a_lat = |v · ω_yaw|` (fart fra GPS × yaw-rate fra gyro-X)
- Arm: `yaw > TURN_ON_YAW_DPS` (12 dps) **og** `a_lat > TURN_ON_LATACC` (1,5 m/s²) → fryser utretting og retraherer begge plan til nøytral, så skroget får krenge fritt inn i svingen
- Slipp: `yaw < TURN_OFF_YAW_DPS` (7 dps) **eller** `a_lat < TURN_OFF_LATACC` (1,0 m/s²), holdt i `TURN_DEBOUNCE_MS` (500 ms) → tilbake til normal roll-regulering
- **Failsafe:** hard maks-tid `TURN_MAX_MS` (4000 ms) — inhiberinga kan aldri stå låst permanent (mot hengende yaw-signal/bias)
- **Hvorfor yaw-gulvet (bug fikset):** uten det blir `a_lat`-terskelen ved planing en absurd lav yaw-rate (3–4 dps @30–36 kn). Normal bølge-/kursholdings-yaw (±4 dps) låste da inhiberinga permanent → flappene retrahert og en vedvarende slagside ble *målt men aldri korrigert*. Reprodusert og verifisert i `analysis/sim_control.py` (dagens-vs-fiks).
- Bevisst **ikke** brukt: `v·ω`-subtraksjon fra aY for å kompensere sideakselerasjon i selve rollmålingen — kalibreringsdata viste svak korrelasjon (r=0,15) og forverret roll-estimatet, siden båten koordinerer svingen selv
- **Tuning gjenstår:** verifiser 12/7 dps-tersklene i ny 8-talls test over 17–36 kn

**Parametere som faktisk brukes:**
- `kP` (GUI viser ×100): proporsjonalforsterkning — typisk 10–20
- `rollDeadbandDeg`: ingen korrigering innenfor ±X grader
- `fusionAlpha`: komplementærfilter gyro vs. akselerometer
- `fullStrokeMs`, `maxDeployFrac`, `neutralFrac`: mekaniske grenser
- `speedOnKn` / `speedOffKn`: fartslås med hysterese
- `rollSetpointDeg`, `mountingOffsetDeg`, `rollSign`: kalibrering
- `relayInvert`: 0 = normal, 1 = snu opp/ned i programvare (for omvendt relékobling)

**Parametere i struct men ikke i bruk (GUI skjult):** `kI`, `cmdTauSec`, `gyroSign`

## Verifisert på benk (2026-07-18)

- GPS: 38400 baud (ikke u-blox-default 115200)
- IMU-fortegn: `rollSign = -1` (styrbord lav → positiv roll på skjermen)
- `gyroSign`-feltet ignoreres — `rollSign` brukes for begge akser (fortegn bakt inn via geometrien `d(atan2)/dt = -ωz`)
- Akselerometer aX/aY median-5-filtrert før roll beregnes (fjerner I2C-glitcher/spikes)
- `PIN_BNO_RST = -1` — RST på BNO055 er ikke koblet til ESP32
- BLE: NimBLE 2.5.0 (se merknad under)
- GPIO-pins for OPP/NED byttet ift. opprinnelig kobling

## Bibliotek (Arduino IDE) for `autotrim_v1`

**NimBLE-Arduino 2.5.0** (ikke eldre — se under), Adafruit BNO055 (+ Adafruit Unified Sensor + BusIO). GPS parses internt — ingen TinyGPSPlus.

### NimBLE-versjon — viktig

NimBLE-Arduino < 2.3.8 krasjer med `assert(mu->handle)` i `npl_os_freertos.c` på esp32-kjerne 3.3.7 og nyere. Bruk **2.5.0** (inkludert i `firmware/test/libraries/NimBLE-Arduino/`).

Installer i Arduino IDE: kopier `firmware/test/libraries/NimBLE-Arduino/` til `Documents/Arduino/libraries/NimBLE-Arduino/`. Pass på at `exp_nimble_mem.c` i `src/nimble/esp_port/port/src/` er tom (den er en duplikat av `esp_nimble_mem.c` og gir linkerfeil).

### BLE-robusthet (hardning)

For å unngå at BLE «henger seg» og ikke kommer i gang igjen uten reflash:

- **Ingen bonding:** `setSecurityAuth(false,false,false)` + `deleteAllBonds()` ved hver init.
- **BLE-watchdog (dempet):** hvert 5. s, kun når frakoblet — holder annonseringen i live (lettvekt). Full re-init (`deinit`+`begin`) skjer KUN ved reell svikt: fri heap < 15 KB. Ikke periodisk re-init lenger — det var årsaken til sporadiske reboots.
- **NVS-recovery:** ved krasj-reset (`ESP_RST_PANIC`/WDT) slettes NVS og MCU-en starter om. **OBS:** sletter hele NVS inkl. lagrede parametere (faller til default).

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

`autotrim_control.html` (i repo-rota) hostes på GitHub Pages (krever HTTPS for Web Bluetooth på mobil). Åpnes i Chrome, kobler til BLE-enheten «Autotrim». GUI b20260718.

**Knapper og funksjoner:**
- **Autotrim: PÅ/AV** — toggle for autoEnabled (grønn = aktiv)
- **Debug: PÅ/AV** — kobler ut farts- og GPS-krav for benk-test (oransje = aktiv); slås alltid av ved omstart
- **BB OPP / BB NED / SB OPP / SB NED** — manuell relé-puls; aktiveres kun når Debug er på; lyser grønt ved aktivt relé
- **NEUTRAL / HOME** — kjører begge plan opp
- **Gjenopprett anbefalte verdier** — tre preset-sett (Fabrikk/Sport/Glatt)
- **Avanserte innstillinger → Snu opp/ned** — aktiverer `relayInvert` for omvendt relékobling

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
| 51 | relayInvert | uint8 | 0=normal, 1=snu opp/ned |
| 52 | mountingOffsetDeg | float | Kompenserer for skjev sensormontering |

> **OBS:** Når struct utvides og PARAMS_VERSION bumpes i firmware, må `dv.setUint8(2, <ny versjon>)` i GUI `buildParams()` oppdateres tilsvarende.
