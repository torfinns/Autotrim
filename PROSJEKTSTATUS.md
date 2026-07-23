# Autotrim — prosjektstatus / gjenopptakspunkt

_Sist oppdatert: 2026-07-18. Les denne først når du gjenopptar i VS Code._

## Kort status
Firmware kompilerer og kjører. Kalibreringskjøring (8-tall @ ~26 kn) er gjort og analysert.
Beslutning sving: bruk **inhibering** (ikke `v·ω`-kompensasjon) — se IMU-avsnitt.

---

## IMU — hva vi bruker (kjernen)
- **Tilt / roll:** akselerometer `atan2(aY, aX)`, **median-5-filtrert**, glattet med gyro-Z (roll-rate) i komplementærfilter. Én `rollSign`.
- **Sving:** gyro-X (yaw-rate) × GPS-fart = `v·ω` (sideakselerasjon). Brukes til svingdeteksjon.
- **Ikke** brukt: `v·ω`-subtraksjon fra aY (data viste r=0.15, gjorde roll verre — båten koordinerer svingen selv).

---

## Firmware (`firmware/autotrim_v1/` — SKAL DØPES OM, drop `_v1`)
Implementert og verifisert-kompilert:
- **Auto default PÅ** (`autoEnabled=1`) — virker uten mobil.
- **Ingen boot-homing** — starter i STANDBY, intet 1 s opp ved (re)boot.
- **Median-5** på accel aX/aY før roll (fjerner spikes).
- **Én `rollSign=-1`** — gyro-fortegn bakt i kode via geometri (`d(atan2)/dt=-ωz`); `gyroSign` ubrukt.
- **Sving-release (A):** `|v·ω_yaw|` > 1.5 m/s² (av 1.0, debounce 800 ms) → fryser utretting + retraher til nøytral så båten krenger fritt inn. Hysterese. Terskler = konstanter (`TURN_ON_LATACC` / `TURN_OFF_LATACC` / `TURN_DEBOUNCE_MS`) — tunes ved re-flash.
- **BLE (NimBLE 2.x):** ingen bonding (`setSecurityAuth(false,false,false)` + `deleteAllBonds`), **dempet watchdog** (re-advertise alltid; full re-init KUN ved heap < 15 kB — ikke periodisk, som var reboot-årsaken).
- **Testmodus** (`testBypass`) + manuelle relé-kommandoer 10–14 (kun i testmodus).
- `relayInvert`-param finnes (fra VS Code-arbeid).

---

## Verifiserte verdier (ikke endre uten grunn)
| Ting | Verdi |
|---|---|
| GPS | 38400 baud, UART2 (RX=16, TX=17) |
| IMU | BNO055 @ 0x28, SDA=21, SCL=22, RST=-1 (intern POR) |
| Relé-pinner | LU=25, LD=33, RU=27, RD=26 |
| Relé | SIP-1A**03** (3 V), direkte GPIO — virker OK |
| Fortegn | `rollSign=-1` (styrbord lav → positiv roll). 0° = BNO-skrift vannrett |

**Kabelfarger, skjøtekabel → Lenco:** 12V=**grå**, LU=hvit, LD=rød, RU=**orange**, RD=grønn.
(Panel-side: 12V=svart, RU=brun. Begge i figurene.)

---

## Sjøtest 2026-07-18 — funn og fiks
**Reguleringsbrist (alvorlig): sving-inhibering låste seg → slagside målt men ikke korrigert.**
- Årsak: `_inTurn` armes av et yaw-kast, men slippes bare når `latAcc < 1.0 m/s²`. Ved
  planing tilsvarer det en absurd lav yaw-rate (3–4 dps @30–36 kn). Normal bølge-/
  kursholdings-yaw (±4 dps) holder debouncen i live → inhibering låst, flappene retrahert.
- Verifisert i `analysis/sim_control.py`: dagens kode står 40–82 % «i sving» med bølge-yaw
  og lar +6° slagside stå ukorrigert. Med fiks: <2 % «i sving», roll < 0,6°.
- Fiks (implementert i firmware): absolutt yaw-gulv `TURN_ON_YAW_DPS=12` / `TURN_OFF_YAW_DPS=7`,
  debounce 500 ms, og hard maks-tid `TURN_MAX_MS=4000` (kan aldri låse permanent).
- PID ikke nødvendig: selve regulatoren fungerer (scenario uten yaw korrigerer fint).
  Feilen lå i sving-gaten, ikke regulatortypen. `kI`-param er forresten ubrukt i koden.

**Offset-input i dashboard:** minus (± -knapp) + komma/punktum aksepteres nå; ugyldig tall
gir rød ramme + varsel i stedet for stille 0. `mountingOffsetDeg` = -2,5° reflekterer reell
monterings-/kjøreattitude (forventet kalibreringskonstant, ikke en feil).

## Neste steg (i prioritert rekkefølge)
1. **Døp om firmware** i VS Code: `firmware/autotrim_v1/` → f.eks. `firmware/Autotrim_Firmware/` + fil likt. Legg evt. `#define FW_VERSION "..."` + skriv ut i `setup()`. Versjoner spores i git, ikke i filnavn.
2. **Commit + push fra VS Code / GitHub Desktop.** ⚠️ IKKE push sandkasse-commitene (`3328ed5`/`2e2f58d`) — de kan ha truncerte snapshots fra fil-sync-rot. Windows-fila er fasit.
3. **Ny testtur:** verifiser den fiksede sving-release i 8-tall over **17–36 kn**; finjuster yaw-gulvet `TURN_ON_YAW_DPS=12` / `TURN_OFF_YAW_DPS=7` (og evt. `TURN_MAX_MS`). Følg `rel:`/tilstand i USB-debug. Sjekk at slagside nå faktisk korrigeres i lett sjø.
4. **BLE-robusthet:** hvis reboots vedvarer tross dempning → vurder nRF52-port (fase 2) eller WiFi-AP.
5. **NVS-krasj-recovery** sletter fortsatt hele NVS (nullstiller params) — vurder å bevare params (sjelden nå som BLE er dempet).

**Parkert / valgfritt:** aktiv koordinert-sving (bank aktivt inn, Humphree-stil) = fase B. ULN2803-driver strøket (1A03 direkte virker).

---

## Kalibrering / test-arbeidsflyt
1. Flash `firmware/test/Autotrim_Calibration_Logger` — auto-logger IMU + GPS @ 25 Hz til intern flash (LittleFS). Kutt strøm når ferdig.
2. Hjemme: `python analysis/grab_calib.py COM10` → `calib.csv` (sender `d`, fanger dump).
3. `python analysis/analyze_calibration.py calib.csv` → median-filtrert plott + regresjon.
   (USB-kommandoer i loggeren: `d`=dump, `i`=info, `p`=pause, `e`=slett.)

---

## Filoversikt (nøkkel)
- `firmware/autotrim_v1/` — hovedfastvare (døpes om).
- `firmware/test/` — testsketcher: `gps_test_arduino`, `imu_test_arduino`, `ble_test_arduino`, `relay_test_arduino`, `autotrim_bench_test`, `Autotrim_Calibration_Logger`.
- `firmware/test/libraries/` — vendrede bibliotek (NimBLE 2.5.0, Adafruit BNO055/BusIO/Sensor).
- `autotrim_control.html` (repo-rot) — Web Bluetooth-dashbord (GitHub Pages).
- `analysis/` — `grab_calib.py`, `analyze_calibration.py`, `calib.csv`.
- SVG-er: `Autotrim_koblingsskjema`, `Autotrim_relekort_detalj`, `Autotrim_rele_mcu_skjema`, `Autotrim_layout_dimensjoner`, `Autotrim_innfesting_detalj`.
- `Autotrim_systemarkitektur.md`, `Autotrim_kobling_og_IO.md`, `README.md`.

---

## Kjente feller
- **Fil-sync:** Cowork-sandkassens terminal/git kan vise *truncerte* snapshots av filer under samtidig redigering. Windows-fila (VS Code) er fasit — commit derfra.
- **Dashbord:** Web Bluetooth krever HTTPS → må hostes (GitHub Pages). Kun én BLE-tilkobling om gangen; lukk nRF Connect før dashbordet.
- **Reed 3,3 V:** 1A03 (3 V) er OK direkte; 1A05 (5 V) ville vært marginalt.
