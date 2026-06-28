# Autotrim — Systemarkitektur

**Prosjekt:** DIY automatisk trimplan-styring for båt, parallellkoblet Lenco trimplansystem (kontrollboks 30077-001)
**Versjon:** 0.1 (utkast) · **Dato:** 2026-06-17

---

## 1. Premiss og mål

Autotrim-en skal automatisk holde båten i vater (rette opp sideveis slagside / roll) ved å styre Lenco-trimplanene, **i parallell med de eksisterende manuelle bryterne**. Systemet skal:

- Kunne brukes både **manuelt** (eksisterende brytere uberørt) og **automatisk** (dette prosjektet).
- **Kun gripe inn over en gitt fart** (planing/marsjfart), ellers ligge passivt.
- Kun korrigere **roll** (sideveis vinkel) — *ikke* pitch (langsgående).
- Til enhver tid holde **ett av de to trimplanene helt oppe**; bare ett plan deployeres om gangen.
- Filtrere bort bølge-/heave-støy slik at planene ikke jager (lavpass ~5 s).
- Konfigureres og overvåkes fra en **app over Bluetooth (BLE)**.

---

## 2. Blokkdiagram

```
                    +12V båtnett
                         │
              ┌──────────┴───────────┐
              │  DC-DC 12-24V → 5V/3A │
              └──────────┬───────────┘
                         │ 5V
        ┌────────────────┼─────────────────────────┐
        │                │                          │
   ┌────┴─────┐    ┌─────┴──────┐            ┌───────┴────────┐
   │ BNO055   │    │   ESP32     │            │ 4× Reed-relé   │
   │ (IMU)    │◄──►│ (CP2102)    │───────────►│ modul (SIP-1A05)│
   │ I2C      │I2C │             │   GPIO     └───────┬────────┘
   └──────────┘    │  - fusjon   │                    │ NO-kontakter
   ┌──────────┐    │  - regulator│          ┌─────────┴───────────┐
   │ TBS M10N │───►│  - logikk   │          │  Lenco kontrollboks  │
   │ GPS UART │UART│  - BLE      │          │      30077-001       │
   └──────────┘    └─────────────┘          │  (parallell m/ bryter)│
                         ▲ BLE              └──────────┬───────────┘
                         │                             │ motorstrøm
                   ┌─────┴─────┐                ┌───────┴────────┐
                   │  Mobil-app│                │ Trimplan-aktuator│
                   │ (BLE)     │                │  venstre / høyre │
                   └───────────┘                └─────────────────┘
```

**Signalflyt:** IMU (roll) + GPS (fart) → ESP32 estimerer rollvinkel og fart → kontroll-logikk → reléer lukker bryterkontaktene i Lenco-boksen → boksen driver aktuatorene. Appen leser status og setter parametere over BLE.

---

## 3. Komponenter og roller

| Komponent | Rolle | Grensesnitt | Nøkkeldata (verifisert) |
|---|---|---|---|
| **ESP32 (CP2102) + breakout** | Hjerne: sensorlesing, fusjon, logikk, BLE | — | 3,3 V logikk, BLE + WiFi innebygd |
| **BNO055 9-DOF** | Rollvinkel (akselerometer + gyro) | I2C (0x28/0x29) | Innebygd sensorfusjon; kan også gi rå accel/gyro. 3,3 V |
| **TBS M10N GPS** | Fart over grunn (SOG) til fartslås | UART (NMEA/UBX) | u-blox M10, std. 9600 baud, oppdaterer ≥1 Hz |
| **4× Reed-relé modul SIP-1A05** | Bryter Lenco-signallinjene (LU/LD/RU/RD) | GPIO inn, NO-kontakt ut | Spole 5 V/~10 mA/500 Ω; kontakt ≪ Lenco-signalstrøm |
| **DC-DC 12-24→5V 3A** | Strømforsyning til 5 V-skinne | — | Mater ESP32 (via 5V/VIN), relémoduler, sensorer (3,3V fra ESP) |
| **Lenco kontrollboks 30077-001** | Driver aktuatorene; auto-retract | Bryter-signallinjer + orange retract | Knapp = 12V (svart) sluttet til funksjonslinje |

> **Spenningsnivå:** BNO055 og GPS er 3,3 V-enheter — kobles på ESP32 sin 3,3 V-utgang og I2C/UART (3,3 V logikk). Relémodulene tar 5 V spole, men styreinngangen er som regel logikk-kompatibel; verifiser at de trigges fra 3,3 V GPIO (se §10).

---

## 4. Relé-grensesnitt mot Lenco (verifisert kobling)

Måling på 30077-001: **svart = 12 V**, og knappetrykk slutter 12 V fra svart til den aktuelle funksjonsledningen. Reléene legges i **parallell** med bryterkontaktene — NO-kontakt mellom svart og funksjonsledning:

| Funksjon | Lenco-ledning | Relékontakt (NO) | ESP32 GPIO (forslag) |
|---|---|---|---|
| **LU** – venstre plan opp | hvit | svart ↔ hvit | f.eks. GPIO 25 |
| **LD** – venstre plan ned | rød | svart ↔ rød | f.eks. GPIO 26 |
| **RU** – høyre plan opp | brun | svart ↔ brun | f.eks. GPIO 27 |
| **RD** – høyre plan ned | grønn | svart ↔ grønn | f.eks. GPIO 14 |

**Konsekvenser av parallellkobling:**

- Manuell bryter og autotrim virker uavhengig — begge kan slutte samme linje. Manuell betjening overstyrer i praksis fordi den alltid er tilgjengelig.
- Reléene må aldri lukke **U og D på samme side samtidig** (motstridende kommando) — sperres i fastvare (interlock).
- Signallinjen fører kun styresignal (lav strøm); motorstrømmen håndteres internt i boksen. Reed-reléet (kontakt tåler 12 V, ~0,5 A bryte) er rikelig. *Mål likevel strømmen ved trykk for margin (§10).*

---

## 5. Montering og akse-kart (IMU)

IMU monteres med **X opp** (vertikalt ut av båten) og **Y på tvers** (babord–styrbord). Da blir den **langsgående aksen Z** (baug–hekk).

- **Roll** = rotasjon om Z (langsgående akse).
- I vater peker tyngdekraften langs X. Slagside vipper g-vektoren inn i Y:

```
roll_accel ≈ atan2( a_Y , a_X )      // vinkel fra accel
roll_rate  ≈ ω_Z                      // rollrate fra gyro (om Z)
```

- **Pitch ignoreres** bevisst (kun roll styres).
- Bruk BNO055 sin akse-remap, eller håndter mappingen i kode. Fortegn på roll kalibreres ved sjøprøve (se §8 og §11).

### 5.1 Mekanisk innfesting og marint vern

Fysisk plassering og mål er dokumentert i `Autotrim_layout_dimensjoner.svg`; *hvordan* kortene holdes fast er vist i `Autotrim_innfesting_detalj.svg`. Anbefalt prototype-«stack» for marint miljø:

- **Printet bunn-«sled» (PETG/ASA, ikke PLA)** med **heat-set M3 messinginnsatser** og **M3 standoffs**. Gjengelås (Loctite 243) eller nylock på alt — båt = konstant vibrasjon.
- **IMU på egen stiv L-brakett**, boltet rett i sleden, **aldri skum-/mykmontert**. Loddrett, X opp, rettet etter båtaksene. Dette er en forutsetning for korrekt rollvinkel og fortegn (§5, §8.2) — mykt feste gir forsinkelse og feil vinkel.
- **Pluggbare terminaler / JST med lås** mellom kort og bokser, så relékortet kan tas ut for service uten lodding.
- **Conformal coating** (akryl/silikon) på alle kort — største enkelttiltak mot saltfukt og kondens.
- **IP65-boks med pakkede kabel-gland + innvendig strekkavlastning** og en **trykkutligningsventil (Gore-membran)** mot kondens. Tinnbelagt ledning, dielektrisk fett på terminaler.
- **Relékort i egen boks** (holder relé-magnetfelt unna BNO055-magnetometeret), samme innfestingsprinsipp.

> **Sikkerhetskobling (§10):** ingen innfesting må kunne klemme eller kortslutte ledere ved vibrasjon. Mekanisk feil skal ikke kunne hindre manuell betjening eller failsafe — disse er elektrisk uavhengige (parallellkobling).

---

## 6. Roll-estimering og støyavvisning

Bølger gir kortvarig heave/akselerasjon som forurenser ren accel-vinkel. Anbefalt: **komplementærfilter** som kombinerer gyro (god på kort sikt, men drifter) og accel (god på lang sikt, men bråkete fra heave):

```
roll = α · (roll_forrige + ω_Z · dt)  +  (1 − α) · roll_accel
```

- α nær 1 (f.eks. 0,98–0,995) → gyro dominerer kortsiktig, accel korrigerer drift sakte. Dette demper heave/bølgestøy.
- Alternativt kan BNO055 sin innebygde fusjon (IMU-modus, relativ orientering uten magnetometer) brukes direkte — vurderes mot egen komplementærfilter.
- **Pådragsfilter:** legg i tillegg et lavpass (~5 s tidskonstant) på *styresignalet/settpunktet*, slik at trimplanene ikke justeres på hver bølge. Dette er to separate filtre: ett raskt for vinkelestimat, ett tregt (~5 s) for pådrag.

---

## 7. Fartslås

Autotrim aktiveres **kun over en konfigurerbar fartsgrense** (SOG fra GPS):

- Bruk **hysterese** for å unngå klapring rundt grensen. Startverdier: **aktiver ved > 17 knop, deaktiver ved < 14 knop** (konfigurerbart i app).
- Krev gyldig GPS-fix og rimelig HDOP før fart regnes som gyldig; ved tap av fix → fall til standby (planene står, ingen auto-inngrep).
- Eventuelt minste tid over grense før aktivering (debounce).

---

## 8. Kontroll-logikk

### 8.1 Tilstander (state machine)

```
STANDBY ──(fart > grense & GPS ok & auto på)──► ACTIVE
ACTIVE  ──(fart < grense | GPS tap | auto av)──► STANDBY
   *Manuell betjening er alltid mulig uavhengig av tilstand.*
```

- **STANDBY:** ingen auto-kommandoer. Reléene hviler. Manuelle brytere virker som normalt.
- **ACTIVE:** regulerer roll mot settpunkt (normalt 0° = vater, konfigurerbart).

### 8.2 «Ett plan alltid oppe»-strategi

Bare **ett** trimplan deployeres om gangen — det andre holdes/parkeres helt oppe.

**Bekreftet fortegn (måling): venstre plan ned ⇒ båten tipper mot styrbord.** Da retter venstre plan opp en babord slagside, og høyre plan en styrbord slagside:

- Lutning mot **babord** (babord lav) → aktivt plan = **venstre** (deploy LD, tipper mot styrbord = vater), høyre parkeres opp (RU).
- Lutning mot **styrbord** (styrbord lav) → aktivt plan = **høyre** (deploy RD), venstre parkeres opp (LU).
- Når lutningen krysser midten skiftes aktivt plan: det forrige kjøres helt opp før/mens det nye deployeres.

### 8.3b Posisjonsestimering uten sensor

Planene har ingen posisjonssensor. Posisjon estimeres med ren tidsintegrasjon:

- **Ved hver oppstart:** kort homing — kjør **begge plan opp i 1 s** som referansenudge. NB: 1 s garanterer ikke fullt opp fra vilkårlig startposisjon (antar planene er nær oppe ved boot). Bruk «Begge opp»/re-kal for full nullstilling (kjører hele slaglengden).
- Deretter logges **antall tideler (0,1 s)** hvert plan har vært kjørt ned. Posisjon ≈ akkumulert ned-tid − akkumulert opp-tid (klemt til [0, full slaglengde]).
- **Nøytral stilling:** begge plan litt over horisontal/plan stilling — settes ved å kjøre opp til referanse og så ev. en liten, kjent ned-tid.
- **Ved tvil/usikkerhet** (drift, oppstart, sensorfeil, krysning) → **kjør begge mer opp** (konservativ/sikker retning). Re-referér jevnlig ved å kjøre full opp-puls (lengre enn full slaglengde).

### 8.3 Regulator

- Enkel **P- eller PI-regulator** på rollfeil → ønsket deployeringsgrad på aktivt plan.
- Aktuatoren styres ved **pulsbredde** (korte ned-/opp-pulser) siden den bare har på/av via reléene; pulslengde ∝ pådrag.
- **Dødbånd** rundt 0° (f.eks. ±1–2°) så systemet ikke jager småbevegelser.
- **Rate-/pådragsbegrensning** (~5 s lavpass, §6) hindrer hyppige korreksjoner.
- **Interlock:** aldri U+D samtidig på samme side; maks samtidig én aktiv ned-kommando.

---

## 9. BLE-grensesnitt (oversikt)

ESP32 eksponerer en GATT-tjeneste for **Android-app (BLE)**. Foreløpig parameter-/status-sett (detaljeres når vi tar app-delen):

**Innstillinger (skriv):** auto på/av · fartsgrense (på/av med hysterese) · roll-settpunkt · dødbånd · regulatorforsterkning (P/I) · lavpass-tidskonstant · maks deployeringsgrad · fortegn/kalibrering.

**Status (les/notify):** estimert rollvinkel · rå accel/gyro (debug) · SOG og GPS-fix · tilstand (STANDBY/ACTIVE) · aktivt plan + estimert posisjon · relé-status · feilflagg.

---

## 10. Sikkerhet og failsafe

- **Manuell overstyring** er alltid tilgjengelig (parallellkobling) — autotrim kan aldri «låse ute» føreren.
- **Failsafe ved feil:** mister ESP strøm / henger watchdog → reléene faller til hvile (ingen kommando) → planene **holder** sin posisjon (de jager ikke), og fører kan korrigere manuelt. Merk: Lenco trekker planene helt opp først når den orange retract-/hovedstrømmen fjernes (nøkkel av) — *ikke* automatisk ved ESP-feil. Vurder å la autotrim kunne kommandere «begge opp» som aktiv sikker tilstand før den går til STANDBY.
- **Watchdog** på ESP32 som faller til STANDBY ved feil i sensor/GPS/loop.
- **GPS-tap eller urimelig IMU-data** → STANDBY.
- **Interlock i fastvare** mot motstridende relékommandoer.
- **Maks deployerings-/kjøretid** per kommando som hard grense, så et hengende relé ikke kjører et plan til endestopp i det uendelige.

---

## 11. Punkter å verifisere før bygging

1. **Relé-styreinngang vs. 3,3 V:** bekreft at relémodulene trigges sikkert fra ESP32 GPIO (3,3 V). Hvis de krever 5 V-nivå, bruk en enkel nivåtilpasning / transistor / ULN2003.
2. **Lenco-signalstrøm:** mål strømmen som flyter når en knapp/relé slutter 12 V til funksjonslinjen, for å bekrefte god margin mot reed-kontaktens grense (~0,5 A bryte).
3. **Roll-fortegn og «aktivt plan»-konvensjon:** bekreftes ved sjøprøve (lutning ↔ hvilket plan retter den opp). Gjøres konfigurerbart i app.
4. **Aktuator-kjøretid (full slaglengde):** mål tid opp/ned for posisjonsestimering uten posisjonssensor.
5. **Fartsgrense og hysterese:** finn fornuftige verdier for den aktuelle båten (når den planer).
6. **Felles jord/referanse:** ESP/relé-jord vs. Lenco/båtens 12V-jord må være samme referanse.

---

## 12. Neste steg

- **Kobling & IO:** detaljert pinplan ESP32, koblingsskjema relémodul ↔ Lenco, strøm/jord-fordeling.
- **Kontrolllogikk & kode:** fastvare-struktur (loop, fusjon, state machine, regulator, BLE).
- **App:** GATT-design og plattformvalg (Android/iOS/web-BLE).

*Kilder for komponent-/Lenco-research er listet i chatten.*
