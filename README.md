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
- BLE: NimBLE 2.x

## Bibliotek (Arduino IDE) for `autotrim_v1`

NimBLE-Arduino 2.x, Adafruit BNO055 (+ Adafruit Unified Sensor + BusIO). GPS parses internt — ingen TinyGPSPlus.

## Dashbord (GitHub Pages)

`app/autotrim_control.html` kan hostes på GitHub Pages (krever HTTPS for Web Bluetooth på mobil). Åpnes i Chrome, kobler til BLE-enheten «Autotrim».
