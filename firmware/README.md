# Autotrim — ESP32 fastvare

Modulær Arduino-fastvare (PlatformIO) for automatisk trim av båt, parallellkoblet Lenco 30077-001.
Se også `../Autotrim_systemarkitektur.md` og `../Autotrim_kobling_og_IO.md`.

## Filstruktur

| Fil | Ansvar |
|---|---|
| `src/config.h` | Pinner, løkke-rater, BLE-UUID-er |
| `src/params.h/.cpp` | Justerbare parametere + lagring i NVS |
| `src/types.h` | Tilstander, side, telemetri-struct, kommandoer |
| `src/imu.h/.cpp` | BNO055 (AMG) → rollvinkel via komplementærfilter |
| `src/gps.h/.cpp` | TBS M10N (UART2) → fart over grunn + fix |
| `src/relays.h/.cpp` | 4 reléer med interlock og sikker av-tilstand |
| `src/control.h/.cpp` | State machine, fartslås, posisjonsestimat, PI-regulator |
| `src/ble_iface.h/.cpp` | NimBLE GATT: parametere, telemetri, kommando |
| `src/main.cpp` | Scheduler som binder alt sammen |

## Bygg

```
pio run                 # kompiler
pio run -t upload       # last opp
pio device monitor      # seriell @115200
```
Eller Arduino IDE: installer bibliotekene i `platformio.ini` (`lib_deps`) og legg `src/`-filene i en sketch.

## Kontrollflyt (kort)

1. **BOOT_UP**: reléer av først, så kjøres begge plan opp i 3 s → referanse «helt oppe» (posisjon = 0).
2. **STANDBY**: ingen auto-inngrep; retraherer plan til oppe ved tvil. Manuell betjening uberørt.
3. **ACTIVE** (når `autoEnabled` og fart > `speedOnKn` og GPS-fix og IMU OK):
   - Roll fra komplementærfilter (gyro-Z + accel `atan2(aY,aX)`), heave dempes.
   - `e = roll − settpunkt` (positiv = styrbord lav). Utenfor dødbånd velges aktivt plan:
     **styrbord lav → høyre plan ned**, **babord lav → venstre plan ned** (bekreftet: venstre ned tipper mot styrbord).
   - PI på avviket → ønsket deploy-fraksjon, glattet med ~5 s lavpass.
   - Bang-bang posisjonsregulator med dødbånd kjører aktivt plan til mål; **andre plan alltid opp**.
4. **FAULT** (IMU usunn) eller tap av vilkår → begge opp, tilbake til STANDBY.

Posisjon estimeres uten føler ved å integrere relé-på-tid (ms) mot `fullStrokeMs`. «Ved tvil → opp».

## BLE-grensesnitt

Enhetsnavn: **Autotrim**. Service-UUID i `config.h`. Alle structer er `#pragma pack(1)`, little-endian.

### Parametere — `UUID_CHAR_PARAMS` (read/write, 52 byte)
App skriver hele structen (inkl. `magic=0xA770`, `version=1`).

| Offset | Type | Felt | Default |
|--:|---|---|--:|
| 0 | u16 | magic (0xA770) | — |
| 2 | u8 | version (1) | — |
| 3 | u8 | autoEnabled | 0 |
| 4 | f32 | speedOnKn | 17 |
| 8 | f32 | speedOffKn | 14 |
| 12 | f32 | rollSetpointDeg | 0 |
| 16 | f32 | rollDeadbandDeg | 1.5 |
| 20 | f32 | kP | 0.06 |
| 24 | f32 | kI | 0.01 |
| 28 | f32 | fusionAlpha | 0.98 |
| 32 | f32 | cmdTauSec | 5.0 |
| 36 | f32 | fullStrokeMs | 6000 |
| 40 | f32 | maxDeployFrac | 0.8 |
| 44 | f32 | neutralFrac | 0.0 |
| 48 | i8 | rollSign (+1/−1) | −1 |
| 49 | i8 | gyroSign (+1/−1) | 1 |
| 50 | u8 | testBypass (0/1) | 0 |
| 51 | u8 | reserved | 0 |

`testBypass=1` overstyrer fart- og GPS-fix-kravet (kun benk-test). **Nullstilles alltid til 0 ved boot** av sikkerhetsgrunner — kan ikke bli stående på utilsiktet.

### Telemetri — `UUID_CHAR_TELEMETRY` (read/notify, 39 byte)

| Offset | Type | Felt |
|--:|---|---|
| 0 | u32 | millisUp |
| 4 | f32 | rollDeg |
| 8 | f32 | rollRateDps |
| 12 | f32 | accX |
| 16 | f32 | accY |
| 20 | f32 | sogKn |
| 24 | u8 | gpsFix (0/1) |
| 25 | u8 | sats |
| 26 | u8 | state (0=BOOT_UP,1=STANDBY,2=ACTIVE,3=FAULT) |
| 27 | i8 | activeSide (0=NONE,1=LEFT,2=RIGHT) |
| 28 | f32 | posLeftFrac (0..1) |
| 32 | f32 | posRightFrac (0..1) |
| 36 | u8 | relayBits (bit0 LU,1 LD,2 RU,3 RD) |
| 37 | u16 | faults (bit0 IMU,1 GPS-fix,2 IMU-sanity) |

### Kommando — `UUID_CHAR_COMMAND` (write, 1 byte)
`1`=begge opp · `2`=re-kalibrer (opp +30 %) · `3`=lagre parametere · `4`=nullstill roll (i ro, vater).

**Test/diagnose (virker KUN når `testBypass=1`):** `10`=puls LU · `11`=puls LD · `12`=puls RU · `13`=puls RD · `14`=stopp. Hver puls varer ≈1,5 s med hard maks-tid. Manuell relé-test suspenderer auto-logikken så lenge pulsen varer.

> **Merk:** parametere lagres til NVS automatisk hver gang appen skriver dem (kommando `3` er dermed redundant — beholdt for kompatibilitet).

## Innjustering (sjøprøve)

1. **`fullStrokeMs`**: mål faktisk tid plan bruker fra helt opp til helt ned.
2. **`rollSign`**: bekreft at positiv `rollDeg` = styrbord lav. Snu om nødvendig.
3. **Reléretning**: bekreft at K2 (rød/LD) faktisk kjører venstre plan ned, osv.
4. **`kP`/`kI`/`rollDeadbandDeg`**: start lavt, øk til rolig oppretting uten jaging.
5. **`speedOnKn`/`speedOffKn`**: 17/14 kn som start — juster til når båten planer.
6. **`fusionAlpha`/`cmdTauSec`**: øk for mer demping av bølger.

## Sikkerhet — før første sjøtest

- Verifiser relémodulens trigger-polaritet (`RELAY_ACTIVE_HIGH` i `config.h`) og at kontakten er **åpen gjennom hele boot** før Lenco kobles til.
- Manuell betjening skal alltid virke (parallellkobling). Test på land først.
- Sikring 2–3 A på 12 V inn.

## TODO / videre

- Hardware-watchdog (`esp_task_wdt`) som tvinger sikker tilstand ved heng.
- Evt. felles «arm»-MOSFET på relé-VCC.
- Android-app mot BLE-layouten over.
