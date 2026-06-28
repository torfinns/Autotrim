# Autotrim — Kobling og IO

**Underlag for breadboard-layout og lodding.**
**Versjon:** 0.1 · **Dato:** 2026-06-17 · Hører til [Autotrim_systemarkitektur.md](Autotrim_systemarkitektur.md)

---

## 1. Spenningsnivåer og forsyning

| Skinne | Kilde | Forbrukere | Merknad |
|---|---|---|---|
| **12 V** | Båtnett (via sikring 2–3 A) | DC-DC inn; Lenco svart (12V) på relékontaktene | Sikring nær batteri/+ |
| **5 V** | DC-DC 12-24→5V/3A ut | ESP32 (5V/VIN-pinne), 4× relémodul VCC, TBS M10N GPS | Total last ≪ 1 A → god margin |
| **3,3 V** | ESP32 sin 3V3-utgang (intern regulator) | BNO055 (Vin), I2C pull-ups | BNO055 ~12 mA |
| **GND** | Felles stjernejord | Alt | DC-DC inn-GND = båt minus = Lenco-jord |

**Strømbudsjett (grovt):** ESP32 ~80–150 mA, BNO055 ~12 mA, GPS ~30–40 mA, 4 relé-spoler ~10 mA hver + LED ~ 60–80 mA. Sum godt under DC-DC-ens 3 A.

> **Galvanisk skille:** Reed-reléet skiller spole (5 V/IN/GND-siden) fra kontakt (reed). Lenco svart/funksjonsledninger går **kun** til relékontaktene (COM/NO) — ingen direkte forbindelse til ESP-jord på den siden.

---

## 2. ESP32 pinnekart (forslag — DevKit 38-pin)

| ESP32-pinne | Funksjon | Kobles til | Retning |
|---|---|---|---|
| **GPIO21** | I2C SDA | BNO055 SDA | bidir |
| **GPIO22** | I2C SCL | BNO055 SCL | ut |
| **GPIO16 (RX2)** | UART2 RX | GPS **TX** | inn |
| **GPIO17 (TX2)** | UART2 TX | GPS **RX** | ut |
| **GPIO33** | Relé **LU** (venstre opp) | Relémodul 1 IN | ut |
| **GPIO25** | Relé **LD** (venstre ned) | Relémodul 2 IN | ut |
| **GPIO26** | Relé **RU** (høyre opp) | Relémodul 3 IN | ut |
| **GPIO27** | Relé **RD** (høyre ned) | Relémodul 4 IN | ut |
| **GPIO4** *(valgfri)* | BNO055 RST | BNO055 RST | ut |
| **3V3** | 3,3 V forsyning | BNO055 Vin, I2C pull-ups | — |
| **5V / VIN** | 5 V inn | DC-DC 5V ut | — |
| **GND ×flere** | Jord | Felles GND | — |

**Hvorfor disse pinnene (relé 33/25/26/27):** de ligger **fortløpende på venstre header** (33→25→26→27) → ren 4-leders strip ut mot relékortet, mens sensor/komm går ut høyre side. Alle fire er **RTC-pinner uten boot-glitch** og **ikke strapping** → ligger Hi-Z ved reset og holdes av med pull-down (§5). Bevisst unngått: GPIO14 (kan pulse kort ved boot), GPIO12/15/0/2/5 (strapping), 6–11 (flash), 34–39 (kun inn). I2C (21/22) og UART2 (16/17) er ESP32 sine standardpinner.

> **WROVER-forbehold:** Hvis kortet faktisk er en ESP32-**WROVER** (med PSRAM), er GPIO16/17 opptatt. UART kan da remappes til andre frie pinner (UART-matrisen tillater fritt valg). De fleste CP2102-kort er WROOM-32 der 16/17 er ledige.

---

## 3. Sensorer

### BNO055 (I2C)
| BNO055 | Til |
|---|---|
| Vin | 3V3 |
| GND | GND |
| SDA | GPIO21 |
| SCL | GPIO22 |
| RST | GPIO4 (valgfri) |
| ADR | GND/uberørt → adresse **0x28** |
| PS0/PS1 | GND → **I2C-modus** |

- **I2C pull-ups:** 4,7 kΩ fra SDA→3V3 og SCL→3V3. Mange BNO055-breakouts har dette innebygd — sjekk; ikke doble unødvendig.
- Montering: X opp, Y på tvers (jf. arkitektur §5). Sett akse-remap i fastvare.
- **Fortegn:** kun **én** brukerinnstilling, `rollSign`. Sett den slik at styrbord-lav gir **positiv** roll på skjermen. Gyroens fortegn styres automatisk i koden: `d(atan2(aY,aX))/dt = −ωz` (geometri), så gyro-leddet får motsatt fortegn av `rollSign` i komplementærfilteret. `gyroSign`-feltet er dermed **ikke i bruk**. 0° = skrifta på BNO055-kortet vannrett.

### TBS M10N GPS (UART)
| GPS | Til |
|---|---|
| VCC (5V) | 5V |
| GND | GND |
| TX | GPIO16 (ESP RX2) |
| RX | GPIO17 (ESP TX2) |

- **Kryss TX/RX** (GPS TX → ESP RX). **38400 baud**, NMEA (verifisert på modulen ved bench-test 2026-06-24; ikke 115200 som u-blox-default antok). Logikknivå 3,3 V-kompatibelt.
- Trenger fri sikt mot himmel for fix.

---

## 4. Reléer ↔ Lenco (kontaktsiden)

Hver relémodul: **spoleside** (VCC=5V, GND, IN=GPIO) og **kontaktside** (COM, NO).

| Modul | ESP IN | COM | NO → Lenco-ledning | Funksjon |
|---|---|---|---|---|
| 1 | GPIO33 (LU) | svart (12V) | **hvit** | venstre opp |
| 2 | GPIO25 (LD) | svart (12V) | **rød** | venstre ned |
| 3 | GPIO26 (RU) | svart (12V) | **brun** | høyre opp |
| 4 | GPIO27 (RD) | svart (12V) | **grønn** | høyre ned |

- Alle fire COM samles på Lenco **svart (12V)**. NO til hver sin funksjonsledning. Relé lukket = samme som knappetrykk.
- Ligger i **parallell** med eksisterende bryter — manuell betjening uberørt.

### 4a. Reed-relé SIP-1A05 — pinout og kobling (direkte GPIO-drift)

SIP-1A05 har 4 pinner: **kontakt = pinne 1 & 7**, **spole = pinne 3(+) / 5(−)** med **innebygd diode** (derfor polaritet på spolen, ingen ekstern frihjulsdiode). Reléene drives **direkte fra GPIO** (ingen transistor):

| Pinne | Kobling |
|---|---|
| 3(+) | GPIO (33/25/26/27) — 3,3 V |
| 5(−) | GND (felles) |
| 1 | 12V (Lenco svart, felles buss) |
| 7 | Lenco funksjonsledning (hvit/rød/brun/grønn) |

Anbefalt 10 kΩ pull-down fra hver GPIO til GND (av ved boot). Komplett skjema for alle 4 kanaler: **Autotrim_relekort_detalj.svg**.

> **Merk:** 3,3 V ligger under spolens must-operate (3,75 V). Test at alle fire trekker sikkert inn. Hvis en ikke gjør det, må den spolen mates fra 5 V — som da krever et bryterelement (transistor/MOSFET/ULN2803) på den kanalen.

---

## 5. Sikker oppstart (viktig)

Relémodulenes IN-polaritet må verifiseres (aktiv-høy vs. aktiv-lav). For at **ingen relé trekker ved boot/reset**:

- **Aktiv-høy modul** (IN høy → relé på): legg **10 kΩ pull-down** fra hver IN til GND. ESP-pinne Hi-Z ved boot → IN holdes lav → relé av. ✅ (anbefalt)
- **Aktiv-lav modul** (IN lav → relé på): legg **10 kΩ pull-up** til 5V/3,3V — men da risikerer du kort aktivering før fastvare tar kontroll. Foretrekk aktiv-høy modul, ev. styr via felles «arm»-MOSFET som kobler relé-VCC først når fastvare er klar.
- **Test:** før noe kobles til Lenco, verifiser med multimeter at relékontakten er **åpen** gjennom hele oppstartssekvensen for alle fire kanaler.

I fastvare: sett alle relé-GPIO til LAV (av) som aller første handling, før WiFi/BLE/sensorer.

---

## 6. Jord og forsyningstopologi

```
Batteri + ──[sikring 2-3A]──┬── DC-DC IN+        Lenco svart(12V) ──┬─ COM relé1
                            │                                       ├─ COM relé2
Batteri − ──────────────────┴── DC-DC IN−  (= felles GND)           ├─ COM relé3
                                                                    └─ COM relé4
DC-DC 5V ─┬─ ESP32 5V/VIN
          ├─ relé VCC ×4
          └─ GPS VCC
ESP32 3V3 ─── BNO055 Vin (+ I2C pull-ups)
Felles GND ─ ESP32 GND, relé GND ×4, BNO055 GND, GPS GND, DC-DC IN−/OUT−
```

- **Stjernejord** fra DC-DC GND. Unngå jordsløyfer.
- Relékontaktsiden (Lenco) deler ikke nødvendigvis jord med ESP (galvanisk skilt), men hele anlegget henger uansett på samme batteriminus.

---

## 7. Breadboard / loddesjekkliste

1. DC-DC justert/verifisert til **5,0 V** før ESP kobles på.
2. Verifiser relé-IN-polaritet og **sikker av-tilstand ved boot** (§5) — uten Lenco tilkoblet.
3. I2C-skanning finner BNO055 på **0x28**.
4. GPS gir NMEA på 115200 og får fix utendørs.
5. Test hver relékanal enkeltvis (fastvare-kommando) og mål kontakt-slutning med multimeter — **før** Lenco kobles til.
6. Mål strøm i Lenco-funksjonslinjen ved slutning (margin mot 0,5 A reed-grense).
7. Bekreft interlock i fastvare: aldri U+D samme side samtidig.
8. Først da: koble relékontaktene i parallell med Lenco-bryterne og test på land (lav fart → ingen auto-inngrep), deretter sjøprøve for fortegn/kalibrering.

### 7a. Testmodus (benk/land, uten fart)

`autotrim_v1` har en **testmodus** (BLE-param `testBypass`, offset 50) som overstyrer fart- og GPS-fix-kravet så auto-logikken kan engasjere uten å være i fart:

- I dashbordet: «Test & diagnose» → skru på **Testmodus**, så **Autotrim på**. Vipp IMU-en → riktig plan kjøres ned (styrbord lav → høyre ned, babord lav → venstre ned). Slik verifiseres relé-mappingen mot vinkel på land.
- **Manuell relé-puls** (kommando 10–13, stopp 14): pulser én kanal ≈1,5 s av gangen — kun aktivt når testmodus er på.
- **Sikkerhet:** `testBypass` nullstilles alltid til 0 ved boot, så modusen kan aldri bli stående på til sjøs.

---

## 8. Åpne valg å bekrefte

- Relémodulenes faktiske trigger-polaritet (avgjør pull-down/-up, §5).
- Om DevKit er 30