#!/usr/bin/env python3
"""Generates the MPC edition's params.json, presets.json and layout.conf.

The presets come straight from the desktop plugin's Source/Presets.h, so both editions share one preset list.
Run from anywhere:  python3 mpc/gen.py
"""
import json
import math
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
NB = 8
TYPES = ["Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt"]
CHANNELS = ["Stereo", "Mid", "Side", "Left", "Right"]
SLOPES = ["-6 dB", "-12 dB", "-18 dB", "-24 dB", "-36 dB", "-48 dB"]   # must not start with a digit: a leading number is read as an option index
FMIN, FMAX, QMIN, QMAX = 20.0, 20000.0, 0.1, 18.0


def fpos(f):
    return round(math.log(max(FMIN, min(FMAX, f)) / FMIN) / math.log(FMAX / FMIN), 5)


def qpos(q):
    return round(math.log(max(QMIN, min(QMAX, q)) / QMIN) / math.log(QMAX / QMIN), 5)


# ---------------------------------------------------------------- parameters
def params():
    p = [
        # parameter 0: MPC toggles it on insert and on STOP (NOTES.md), so it is the harmless output trim
        {"key": "out", "name": "Output", "min": -24.0, "max": 24.0, "default": 0.0, "unit": "dB"},
        {"key": "attack", "name": "Dyn Attack", "min": 0.5, "max": 100.0, "default": 5.0, "unit": "ms"},
        {"key": "release", "name": "Dyn Release", "min": 10.0, "max": 1000.0, "default": 120.0, "unit": "ms"},
    ]
    for b in range(1, NB + 1):
        n = "B%d " % b
        p += [
            {"key": "b%d_on" % b, "name": n + "On", "options": ["Off", "On"], "default": 0},
            {"key": "b%d_type" % b, "name": n + "Type", "options": TYPES, "default": 0},
            {"key": "b%d_freq" % b, "name": n + "Freq", "min": 0.0, "max": 1.0,
             "default": round((b - 1) / (NB - 1) * 0.9 + 0.05, 5), "dynamic_display": True},
            {"key": "b%d_gain" % b, "name": n + "Gain", "min": -24.0, "max": 24.0, "default": 0.0, "unit": "dB"},
            {"key": "b%d_q" % b, "name": n + "Q", "min": 0.0, "max": 1.0, "default": qpos(1.0), "dynamic_display": True},
            {"key": "b%d_slope" % b, "name": n + "Slope", "options": SLOPES, "default": 3},
            {"key": "b%d_ch" % b, "name": n + "Channel", "options": CHANNELS, "default": 0},
            {"key": "b%d_dyn" % b, "name": n + "Dynamic", "options": ["Off", "On"], "default": 0},
            {"key": "b%d_thr" % b, "name": n + "Threshold", "min": -60.0, "max": 0.0, "default": -24.0, "unit": "dB"},
            {"key": "b%d_rng" % b, "name": n + "Range", "min": -24.0, "max": 24.0, "default": -6.0, "unit": "dB"},
        ]
    return p


# ---------------------------------------------------------------- presets (parsed from Presets.h)
def num(s):
    return float(s.strip().rstrip("f"))


def parse_band(call):
    m = re.match(r"(\w+)\s*\((.*)\)$", call.strip(), re.S)
    fn, args = m.group(1), [a.strip() for a in m.group(2).split(",")]
    ch = 0
    band = {"type": 0, "freq": 1000.0, "gain": 0.0, "q": 1.0, "slope": 1, "ch": 0, "dyn": 0, "thr": -24.0, "rng": -6.0}

    def chan(a):
        return CHANNELS.index(a) if a in CHANNELS else 0

    if fn == "bell":
        band.update(type=0, freq=num(args[0]), gain=num(args[1]), q=num(args[2]))
        if len(args) > 3: ch = chan(args[3])
    elif fn in ("lowShelf", "highShelf"):
        band.update(type=1 if fn == "lowShelf" else 2, freq=num(args[0]), gain=num(args[1]),
                    q=num(args[2]) if len(args) > 2 else 0.7)
        if len(args) > 3: ch = chan(args[3])
    elif fn in ("lowCut", "highCut"):
        band.update(type=3 if fn == "lowCut" else 4, freq=num(args[0]),
                    slope=int(args[1]) if len(args) > 1 else (3 if fn == "lowCut" else 1),
                    q=num(args[2]) if len(args) > 2 else 0.71)
        if len(args) > 3: ch = chan(args[3])
    elif fn == "notch":
        band.update(type=5, freq=num(args[0]), q=num(args[1]))
    elif fn == "tilt":
        band.update(type=7, freq=num(args[0]), gain=num(args[1]))
    elif fn == "dynBell":
        band.update(type=0, freq=num(args[0]), gain=num(args[1]), q=num(args[2]), dyn=1, thr=num(args[3]), rng=num(args[4]))
        if len(args) > 5: ch = chan(args[5])
    elif fn == "dynShelf":
        t = {"LowShelf": 1, "HighShelf": 2}[args[0]]
        band.update(type=t, freq=num(args[1]), gain=num(args[2]), q=0.7, dyn=1, thr=num(args[3]), rng=num(args[4]))
    else:
        raise SystemExit("unknown preset helper %r" % fn)
    band["ch"] = ch
    return band


def split_calls(body):
    """'a (1, 2), b (3)' -> ['a (1, 2)', 'b (3)'], ignoring // comments"""
    body = re.sub(r"//[^\n]*", "", body)
    calls, depth, cur = [], 0, ""
    for c in body:
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        if c == "," and depth == 0:
            if cur.strip(): calls.append(cur.strip())
            cur = ""
        else:
            cur += c
    if cur.strip(): calls.append(cur.strip())
    return calls


def presets():
    src = open(os.path.join(HERE, "..", "Source", "Presets.h")).read()
    src = src[src.index("static const std::vector<FactoryPreset> presets"):]
    out = []
    for m in re.finditer(r'\{\s*"([^"]+)",\s*"([^"]+)",\s*\{(.*?)\}\s*(?:,\s*([-\d.]+)f)?\s*\}', src, re.S):
        cat, name, body = m.group(1), m.group(2), m.group(3)
        bands = [parse_band(c) for c in split_calls(body)]
        if len(bands) > NB:
            raise SystemExit("%s has %d bands, the MPC edition has %d" % (name, len(bands), NB))
        values = {"out": float(m.group(4) or 0.0), "attack": 5.0, "release": 120.0}
        for i in range(NB):
            k = "b%d_" % (i + 1)
            if i < len(bands):
                b = bands[i]
                values.update({k + "on": "On", k + "type": TYPES[b["type"]], k + "freq": fpos(b["freq"]),
                               k + "gain": b["gain"], k + "q": qpos(b["q"]), k + "slope": SLOPES[b["slope"]],
                               k + "ch": CHANNELS[b["ch"]], k + "dyn": "On" if b["dyn"] else "Off",
                               k + "thr": b["thr"], k + "rng": b["rng"]})
            else:
                values.update({k + "on": "Off", k + "type": "Bell", k + "freq": round(i / (NB - 1) * 0.9 + 0.05, 5),
                               k + "gain": 0.0, k + "q": qpos(1.0), k + "slope": "-24 dB", k + "ch": "Stereo",
                               k + "dyn": "Off", k + "thr": -24.0, k + "rng": -6.0})
        label = name if cat in ("Default", "Utility") else name   # names already carry BB / NJS prefixes
        out.append({"name": label, "values": values})
    return out


# ---------------------------------------------------------------- skin layout
THEME = """style=flat
theme_bg=121419
theme_panel=1a1d24
theme_line=2c313d
theme_ink=e8ebf1
theme_ink_dim=9aa2b2
theme_ink_faint=5c6474
theme_accent=e9a23b
theme_accent_hi=ffd38a
theme_knob_face=222631
theme_knob_ring=2c313d
"""


def band_frame(b, x):
    k = "b%d_" % b
    L = []
    L.append('frame x=%d y=96 w=600 h=520 title="BAND %d"' % (x, b))
    L.append('toggle cx=%d cy=170 label="ON" key=%son' % (x + 70, k))
    L.append('popup cx=%d cy=170 w=220 h=48 label="TYPE" key=%stype' % (x + 270, k))
    L.append('popup cx=%d cy=170 w=170 h=48 label="CHANNEL" key=%sch' % (x + 490, k))
    L.append('knob cx=%d cy=320 r=52 label="FREQ" key=%sfreq' % (x + 110, k))
    L.append('knob cx=%d cy=320 r=52 label="GAIN" key=%sgain' % (x + 300, k))
    L.append('knob cx=%d cy=320 r=52 label="Q" key=%sq' % (x + 490, k))
    for t in (3, 4):   # slope only matters for the cut filters (option index: Low Cut, High Cut)
        L.append('popup cx=%d cy=480 w=150 h=48 label="SLOPE" key=%sslope when=%stype:%d' % (x + 110, k, k, t))
    L.append('toggle cx=%d cy=480 label="DYNAMIC" key=%sdyn' % (x + 270, k))
    L.append('knob cx=%d cy=490 r=42 label="THRESH" key=%sthr' % (x + 385, k))
    L.append('knob cx=%d cy=490 r=42 label="RANGE" key=%srng' % (x + 520, k))
    return L


def layout():
    lines = [THEME.strip(), ""]
    # track-mode Q-Links: gain of every band on bank 1, frequency on bank 2
    lines.append("qlinks_track = " + ",".join(["b%d_gain" % b for b in range(1, NB + 1)] + ["b%d_freq" % b for b in range(1, NB + 1)]))
    for a in range(1, NB + 1, 2):
        b = a + 1
        lines.append("")
        lines.append("[tab BANDS %d-%d]" % (a, b))
        lines += band_frame(a, 20)
        lines += band_frame(b, 660)
        q = []
        for n in (a, b):
            q += ["b%d_%s" % (n, s) for s in ("freq", "gain", "q", "thr", "rng", "type", "ch", "on")]
        lines.append('qlinks "BANDS %d-%d" = %s' % (a, b, ",".join(q)))
    lines += ["", "[tab OUTPUT]",
              'frame x=20 y=96 w=1240 h=520 title="TESSERA EQ"',
              'knob cx=240 cy=330 r=58 label="OUTPUT" key=out',
              'knob cx=560 cy=330 r=52 label="DYN ATTACK" key=attack',
              'knob cx=820 cy=330 r=52 label="DYN RELEASE" key=release',
              'qlinks "OUTPUT" = out,attack,release']
    return "\n".join(lines) + "\n"


def main():
    pl = params()
    json.dump({"name": "Tessera EQ", "params": pl}, open(os.path.join(HERE, "params.json"), "w"), indent=1)
    pr = presets()
    json.dump({"presets": pr}, open(os.path.join(HERE, "presets.json"), "w"), indent=1)
    open(os.path.join(HERE, "layout.conf"), "w").write(layout())
    print("params: %d, presets: %d" % (len(pl), len(pr)))


if __name__ == "__main__":
    main()
