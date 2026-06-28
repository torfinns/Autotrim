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

## Lederfarger — Lenco-kontrollboks (30077-001)

To kabelsegmenter med ulik farge på samme signal:

| Funksjon | Panel-side | Skjøtekabel (båt → boks) |
|---|---|---|
| 12 V | svart | grå |
| LU (venstre opp) | hvit | hvit |
| LD (venstre ned) | rød | rød |
| RU (høyre opp) | brun | orange |
| RD (høyre ned) | grønn | grønn |

Begge gjelder: «Panel-side» er fargene i `Autotrim_kobling_og_IO.md` og relekort-SVG-en; «Skjøtekabel» er den faktiske kabelen fra båten inn til kontrollboksen. Kun **RU** (brun ↔ orange) og **12 V** (svart ↔ grå) skifter farge mellom segmentene.

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

### BLE-robusthet (hardning)

For å unngå at BLE «henger seg» og ikke kommer i gang igjen uten reflash:

- **Ingen bonding:** `setSecurityAuth(false,false,false)` + `deleteAllBonds()` ved hver init. Åpen konfig-link trenger ikke paring, og dette fjerner hele klassen av feil der stale/korrupt bonding-data i NVS hindrer BLE i å starte