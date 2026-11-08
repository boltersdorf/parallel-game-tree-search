"""shared parser for the session .out files. every figure and table reads
through this one loader so a format change breaks loudly in one place.

!!! per thread fields come in two formats, older logs carry interior:leaf:secs,
newer ones interior:leaf:ttcut:secs, both parse"""

import statistics as st
from collections import defaultdict
from pathlib import Path

AN = Path(__file__).resolve().parent

STRATEGIES = ["AB Seq Oracle", "Minmax Seq Oracle", "AB Parallel",
              "AB Par TT ID Asp Ord YBW", "AB Par TT ID Asp Ord",
              "AB Par TT ID Asp", "AB Par TT ID", "AB Par TT",
              "AB Par Ord", "AB Par YBW"]

SKIP = ("=", "ROOTNODES", "CUTOFFAT", "TTSTATS", "ASPSTATS", "run id",
        "results", "depth=", "csv:", "versions:", "positions:", "threads:",
        "cpu:", "mask:", "places:", "PASS", "FAIL", "A4", "BUDGET",
        "NOTE", "WARNING", "AFFINITY", "CPU", "Thread", "Core", "Socket")


def rows(path):
    out = []
    for line in open(path):
        line = line.rstrip("\n")
        if not line or line.startswith(SKIP):
            continue
        bind = None
        if line.startswith("BIND_"):
            bind, line = line.split(",", 1)
        f = line.split(",")
        if len(f) < 14 or f[0] not in STRATEGIES:
            continue
        try:
            r = dict(strategy=f[0], pos=f[1], depth=int(f[2]),
                     threads=int(f[3]), rep=int(f[4]), value=int(f[5]),
                     move=f[6], secs=float(f[7]), interior=int(f[8]),
                     leaf=int(f[9]), ttcut=int(f[10]), ceiling=float(f[11]),
                     cutoffs=int(f[12]), searched=int(f[13]), bind=bind)
        except ValueError:
            continue
        r["nodes"] = r["interior"] + r["leaf"] + r["ttcut"]
        per = []
        for t in f[14:]:
            if "=" not in t:
                continue
            v = t.split("=")[1].split(":")
            if len(v) == 3:      # interior:leaf:secs, ttcut not recorded
                per.append(dict(interior=int(v[0]), leaf=int(v[1]),
                                ttcut=None, secs=float(v[2])))
            elif len(v) == 4:    # interior:leaf:ttcut:secs
                per.append(dict(interior=int(v[0]), leaf=int(v[1]),
                                ttcut=int(v[2]), secs=float(v[3])))
        r["per"] = per
        out.append(r)
    return out


def rootnodes(path):
    """nodes under each root move, keyed like the csv rows"""
    out = defaultdict(list)
    for line in open(path):
        if not line.startswith("ROOTNODES,"):
            continue
        f = line.rstrip("\n").split(",")
        try:
            key = (f[1], f[2], int(f[3]), int(f[4]), int(f[5]))
            out[key] = [int(x) for x in f[6:]]
        except ValueError:
            continue
    return out


def newest(prefix):
    """the newest result file of one session, by the timestamp in the name"""
    c = sorted(AN.glob(f"results/{prefix}_*.out"))
    if not c:
        raise SystemExit(f"no results/{prefix}_*.out, run the session first")
    return c[-1]


def med(v):
    return st.median(v)


def group(rs, **want):
    sel = [r for r in rs if all(r.get(k) == v for k, v in want.items())]
    return sel


def median_of(rs, field, **want):
    sel = group(rs, **want)
    return st.median([r[field] for r in sel]) if sel else None
