"""RSSI / SNR readings vs the SX126x LoRa sensitivity floor (constant per SF/BW), October range test.

Run: python RangeTestData/plot_sensitivity_margin.py [--noise-figure-db 6]
Sensitivity = -174 + 10log10(BW_Hz) + NF + SNR_limit(SF). SF/BW come from each log's ping_ack line.
"""

import argparse
import re

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

from analyze_october_range import COLORS, DISTANCES, OUTPUT, SOURCE, finish, load_log, samples

# Demodulator SNR limit per spreading factor (Semtech SX126x datasheet).
SNR_LIMIT_DB = {7: -7.5, 8: -10.0, 9: -12.5, 10: -15.0, 11: -17.5, 12: -20.0}
NOISE_FIGURE_DB = 6.0   # Typical SX126x receiver noise figure; override with --noise-figure-db.
CONFIG_COLORS = {"1": "#6a51a3", "2": "#2171b5", "3": "#238b45"}


def sensitivity_dbm(sf, bw_khz, nf):
    return -174 + 10*np.log10(bw_khz*1e3) + nf + SNR_LIMIT_DB[int(sf)]


def load_runs(nf):
    runs = []
    for path in sorted(SOURCE.glob("*.csv")):
        m = re.match(r"L([1-5])config([123])(pt2)?$", path.stem, re.I)
        if not m:
            continue
        df, _, _, settings = load_log(path)
        _, bw, sf, *_ = (float(v) for v in settings.split(";")[0].split(","))
        runs.append(dict(name=path.stem, stop=int(m[1]), config=m[2], pt2=bool(m[3]), df=df,
                         sf=int(sf), bw=bw, sens=sensitivity_dbm(sf, bw, nf), snr_lim=SNR_LIMIT_DB[int(sf)]))
    return runs


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--noise-figure-db", type=float, default=NOISE_FIGURE_DB)
    nf = ap.parse_args().noise_figure_db
    plt.rcParams.update({"axes.spines.top": False, "axes.spines.right": False, "font.size": 11})
    runs = load_runs(nf)
    method = f"Medians, 10th-90th percentile shading. Sensitivity = -174 + 10log10(BW) + {nf:g} dB NF + SF SNR limit."

    # Figure 1: Config 2 RSSI and SNR vs distance against the constant floor.
    c2 = sorted((r for r in runs if r["config"] == "2"), key=lambda r: r["stop"])
    x = np.array([DISTANCES[r["stop"]] for r in c2])
    fig, (ax_r, ax_s) = plt.subplots(2, 1, figsize=(10, 9), sharex=True)
    for ax, metric, floor, unit in ((ax_r, "RSSI", c2[0]["sens"], "dBm"), (ax_s, "SNR", c2[0]["snr_lim"], "dB")):
        for rx, color in COLORS.items():
            v = [samples(r["df"], rx, metric).to_numpy() for r in c2]
            ax.plot(x, [np.median(a) for a in v], "o-", color=color, label=f"{rx} median {metric}")
            ax.fill_between(x, [np.quantile(a, .1) for a in v], [np.quantile(a, .9) for a in v],
                            color=color, alpha=.13)
        ax.axhline(floor, color="#cb181d", linestyle="--", linewidth=1.8,
                   label=f"{'Sensitivity' if metric == 'RSSI' else 'Demod SNR limit'}: {floor:.1f} {unit}")
        ax.set_ylim(bottom=floor - 3)
        ax.fill_between([x[0]/1.03, x[-1]*1.03], floor - 3, floor, color="#cb181d", alpha=.08)
        ax.set_ylabel(f"{metric} ({unit})")
        ax.grid(alpha=.25)
        ax.legend(loc="center left" if metric == "RSSI" else "upper right", fontsize=9)
    ax_s.set_xscale("log")
    ax_s.set_xticks(x, [f"L{r['stop']}\n{d:g}" for r, d in zip(c2, x)])
    ax_s.minorticks_off()
    ax_s.set_xlim(x[0]/1.03, x[-1]*1.03)
    ax_s.set_xlabel("Planned range (km, log scale)")
    fig.suptitle(f"Config 2 (SF{c2[0]['sf']}, {c2[0]['bw']:g} kHz): readings vs sensitivity floor", fontsize=15)
    finish(fig, "rssi_snr_vs_sensitivity.png", method)

    # Figure 2: margin above the floor for every configuration at every stop (GS receiver).
    fig, (ax_r, ax_s) = plt.subplots(1, 2, figsize=(13, 5.5), sharex=True)
    for ax, metric in ((ax_r, "RSSI"), (ax_s, "SNR")):
        for cfg, color in CONFIG_COLORS.items():
            for pt2 in (False, True):
                rs = sorted((r for r in runs if r["config"] == cfg and r["pt2"] == pt2), key=lambda r: r["stop"])
                if not rs:
                    continue
                m = [np.median(samples(r["df"], "GS", metric)) - (r["sens"] if metric == "RSSI" else r["snr_lim"])
                     for r in rs]
                r0 = rs[0]
                ax.plot([DISTANCES[r["stop"]] for r in rs], m, "o--" if pt2 else "o-", color=color,
                        markerfacecolor="white" if pt2 else color,
                        label=f"Config {cfg}{' Pt2' if pt2 else ''} (SF{r0['sf']}, {r0['bw']:g} kHz)")
        ax.axhline(0, color="#cb181d", linestyle="--", linewidth=1.8, label="Sensitivity floor")
        ax.set_xscale("log")
        ax.set_xticks(list(DISTANCES.values()), [f"L{k}\n{v:g}" for k, v in DISTANCES.items()])
        ax.minorticks_off()
        ax.set_xlabel("Planned range (km, log scale)")
        ax.set_ylabel(f"{metric} margin above floor ({'dB'})")
        ax.set_title(f"GS median {metric} - {'sensitivity' if metric == 'RSSI' else 'SNR limit'}")
        ax.grid(alpha=.25)
    ax_r.legend(fontsize=9, loc="best")
    fig.suptitle("Link margin above receiver sensitivity, all configurations", fontsize=15)
    finish(fig, "margin_above_sensitivity.png", method.replace(", 10th-90th percentile shading", ""))

    print(f"{'Run':<16}{'SF':>3}{'BW':>7}{'Sens dBm':>10}{'SNR lim':>9}{'GS RSSI':>9}{'RSSI mrg':>9}{'GS SNR':>8}{'SNR mrg':>8}")
    for r in sorted(runs, key=lambda r: (r["stop"], r["name"].lower())):
        rs, sn = np.median(samples(r["df"], "GS", "RSSI")), np.median(samples(r["df"], "GS", "SNR"))
        print(f"{r['name']:<16}{r['sf']:>3}{r['bw']:>7g}{r['sens']:>10.1f}{r['snr_lim']:>9.1f}"
              f"{rs:>9.1f}{rs - r['sens']:>9.1f}{sn:>8.1f}{sn - r['snr_lim']:>8.1f}")
    print(f"\nSaved rssi_snr_vs_sensitivity.png and margin_above_sensitivity.png to {OUTPUT}")


if __name__ == "__main__":
    main()
