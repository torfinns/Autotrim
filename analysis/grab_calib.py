#!/usr/bin/env python3
"""
Hent kalibrerings-loggen fra MCU over USB rett til fil.

Kobler til serieporten, sender 'd' (dump), fanger CSV-en mellom
'CSV START' og 'CSV SLUTT', og skriver til fil.

Bruk:
    pip install pyserial
    python grab_calib.py COM10                 # -> calib.csv
    python grab_calib.py COM10 minlogg.csv     # egendefinert filnavn

Deretter:
    python analyze_calibration.py calib.csv
"""
import sys, time
try:
    import serial
except ImportError:
    sys.exit("Mangler pyserial:  pip install pyserial")

port = sys.argv[1] if len(sys.argv) > 1 else "COM10"
out  = sys.argv[2] if len(sys.argv) > 2 else "calib.csv"
BAUD = 115200

print(f"Åpner {port} @ {BAUD} ...")
ser = serial.Serial(port, BAUD, timeout=2)
time.sleep(2.0)                 # la ESP32 boote ferdig (port-åpning gir reset)
ser.reset_input_buffer()

print("Sender 'd' (dump) ...")
ser.write(b"d")

lines, capturing = [], False
t0 = time.time()
while time.time() - t0 < 120:            # maks 2 min
    raw = ser.readline()
    if not raw:
        if capturing:                    # stille etter start -> antar ferdig
            break
        continue
    s = raw.decode("utf-8", "replace").rstrip("\r\n")
    if "CSV START" in s:
        capturing = True; t0 = time.time(); continue
    if "CSV SLUTT" in s:
        break
    if capturing:
        lines.append(s)

ser.close()

if not lines:
    sys.exit("Fikk ingen data. Sjekk port, at loggeren kjører, og at du har logget noe (send 'i').")

with open(out, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")
print(f"Skrev {len(lines)} linjer til {out}")
print(f"Analyser:  python analyze_calibration.py {out}")
