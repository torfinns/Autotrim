#!/usr/bin/env python3
"""
Autotrim — analyse av kalibrerings-logg (8-talls sving-test).

Leser CSV dumpet fra Autotrim_Calibration_Logger og undersøker om sving-artefakten
kan forklares/kompenseres med koordinert-sving-modellen:  a_lat ≈ v · ω_yaw.

Median-filtrerer alle sensordata (glidende vindu, default 5) før analyse.

Bruk:
    pip install pandas numpy matplotlib
    python analyze_calibration.py calib.csv
    python analyze_calibration.py calib.csv --median 7 --out figur.png

CSV-kolonner (fra loggeren):
    t_ms, aX, aY, aZ, gX_yaw, gY, gZ_roll, sog_kn, cog_deg, roll_raw, amag
"""
import sys, argparse
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

COLS = ["t_ms","aX","aY","aZ","gX_yaw","gY","gZ_roll","sog_kn","cog_deg","roll_raw","amag"]
SENSOR = ["aX","aY","aZ","gX_yaw","gY","gZ_roll","sog_kn","cog_deg"]
KN2MS = 0.514444
DEG2RAD = np.pi/180.0
ROLL_SIGN = -1.0   # verifisert montering

def load(path):
    rows = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for ln in f:
            ln = ln.strip()
            if not ln or ln[0] in "#tT-":      # hopp over kommentar/header/markører
                continue
            parts = ln.split(",")
            if len(parts) != len(COLS):
                continue
            try:
                rows.append([float(x) for x in parts])
            except ValueError:
                continue
    if not rows:
        sys.exit("Fant ingen datarader — er filen riktig CSV-dump?")
    df = pd.DataFrame(rows, columns=COLS)
    df["t"] = (df["t_ms"] - df["t_ms"].iloc[0]) / 1000.0
    return df

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--median", type=int, default=5, help="glidende medianvindu (min 5)")
    ap.add_argument("--yaw-threshold", type=float, default=5.0, help="dps — over dette = sving")
    ap.add_argument("--out", default="calibration_analysis.png")
    a = ap.parse_args()

    df = load(a.csv)
    n = len(df); dur = df["t"].iloc[-1]
    print(f"Lest {n} rader, {dur:.1f} s ({n/max(dur,1):.1f} Hz).")

    # --- medianfilter på alle sensordata (fjerner spikes/utliggere) ---
    W = max(5, a.median)
    for c in SENSOR:
        df[c] = df[c].rolling(W, center=True, min_periods=1).median()
    print(f"Medianfilter: glidende vindu = {W} punkt.")

    # derivert fra filtrerte data
    df["amag"]     = np.sqrt(df.aX**2 + df.aY**2 + df.aZ**2)
    df["roll_raw"] = np.degrees(np.arctan2(df.aY, df.aX)) * ROLL_SIGN
    v   = df["sog_kn"] * KN2MS
    wy  = df["gX_yaw"] * DEG2RAD
    df["a_lat_pred"] = v * wy
    df["roll_comp"]  = np.degrees(np.arctan2(df.aY - df.a_lat_pred, df.aX)) * ROLL_SIGN

    turning = df["gX_yaw"].abs() > a.yaw_threshold
    print(f"Sving-samples (|yaw|>{a.yaw_threshold} dps): {turning.sum()} "
          f"({100*turning.mean():.0f} %).  GPS-fix (SOG>0): {100*(df['sog_kn']>0).mean():.0f} %.")

    if turning.sum() > 10 and df.loc[turning,"a_lat_pred"].std() > 1e-6:
        x = df.loc[turning,"a_lat_pred"].values; y = df.loc[turning,"aY"].values
        slope, intercept = np.polyfit(x, y, 1); r = np.corrcoef(x, y)[0,1]
        print(f"\nI svinger:  aY ≈ {slope:.2f}·(v·ω) + {intercept:.2f}   (r = {r:.2f})")
        print("  slope~±1 og høy r => v·ω forklarer aY (kompensasjon). Nær 0 => aY bærer ikke svingen (inhibering).")
    else:
        print("\nFor lite sving-/fartsdata til regresjon.")

    rms_raw  = np.sqrt(np.mean(df.loc[turning,"roll_raw"]**2)) if turning.any() else float('nan')
    rms_comp = np.sqrt(np.mean(df.loc[turning,"roll_comp"]**2)) if turning.any() else float('nan')
    print(f"\nRMS |roll| i svinger:  rå = {rms_raw:.1f}°   'kompensert' = {rms_comp:.1f}°")

    # ---- plott ----
    fig, ax = plt.subplots(3, 1, figsize=(11, 10))
    tshade = df["t"][turning]
    def shade(axx):
        for t0 in tshade: axx.axvspan(t0-0.02, t0+0.02, color="#ffd27f", alpha=0.5, lw=0)

    ax[0].set_title(f"Rollvinkel (median {W}): rå vs. sving-kompensert (gule felt = sving)")
    shade(ax[0])
    ax[0].plot(df.t, df.roll_raw, label="roll_raw", color="#c0392b", lw=1)
    ax[0].plot(df.t, df.roll_comp, label="roll_comp (aY − v·ω)", color="#2e9bff", lw=1)
    ax[0].set_ylim(-25, 25)                         # <- ±25°
    ax[0].set_ylabel("grader"); ax[0].legend(loc="upper right"); ax[0].grid(alpha=.3)

    ax[1].set_title(f"Målt sideakselerasjon aY vs. modell v·ω_yaw (median {W})")
    ax[1].plot(df.t, df.aY, label="aY (målt)", color="#333", lw=1)
    ax[1].plot(df.t, df.a_lat_pred, label="v·ω_yaw (modell)", color="#27c08a", lw=1)
    ax[1].set_ylim(-3, 3)                            # <- ±3 m/s²
    ax[1].set_ylabel("m/s²"); ax[1].legend(loc="upper right"); ax[1].grid(alpha=.3)

    ax[2].set_title("Korrelasjon i svinger:  aY mot v·ω_yaw")
    if turning.sum() > 2:
        ax[2].scatter(df.loc[turning,"a_lat_pred"], df.loc[turning,"aY"], s=6, alpha=.4, color="#8e44ad")
    ax[2].set_xlabel("v·ω_yaw (m/s²)"); ax[2].set_ylabel("aY (m/s²)"); ax[2].grid(alpha=.3)

    ax[0].set_xlabel("s"); ax[1].set_xlabel("s")
    fig.tight_layout(); fig.savefig(a.out, dpi=120)
    print(f"\nFigur lagret: {a.out}")
    try: plt.show()
    except Exception: pass

if __name__ == "__main__":
    main()
