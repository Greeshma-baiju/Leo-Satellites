#!/usr/bin/env python3
"""
Turns OMNeT++ 6 result files (.sca/.vec, text format) of the LEO reverse-proxy
project into tidy CSV tables used by plot_figures.py.

Pure Python + pandas: no dependency on the omnetpp.scave package, so it runs
outside the IDE as well.

Usage:
    python3 parse_results.py [--results ../simulations/results] [--out csv]

Outputs (in --out):
    runs.csv           one row per run: config, repetition, iteration variables
                       and all derived metrics
    summary.csv        runs.csv averaged over the replications (mean of 3)
    timeseries.csv     selected vectors (time series figures), long format
"""
import argparse
import glob
import math
import os
import re
import shlex
from collections import defaultdict

import pandas as pd

MEASUREMENT_WINDOW_S = 3600.0   # fallback measurement window when the run records none


def parse_sca(path):
    """Returns (runinfo dict, results dict[(module, name)] -> value)."""
    run = {"file": os.path.basename(path), "itervars": {}, "attrs": {}, "config": {}}
    res = {}
    current = None
    with open(path) as f:
        for line in f:
            if not line.strip():
                continue
            try:
                p = shlex.split(line)
            except ValueError:
                p = line.split()
            kind = p[0]
            if kind == "run":
                run["runId"] = p[1]
            elif kind == "attr" and current is None and len(p) >= 3:
                run["attrs"][p[1]] = p[2]
            elif kind == "itervar":
                run["itervars"][p[1]] = p[2].strip('"')
            elif kind == "config" and len(p) >= 3:
                run["config"].setdefault(p[1], p[2])   # first entry = highest precedence
            elif kind == "scalar":
                res[(p[1], p[2])] = _num(p[3])
                current = None
            elif kind in ("statistic", "histogram"):
                current = (p[1], p[2])
            elif kind == "field" and current is not None:
                res[(current[0], current[1] + ":" + p[1])] = _num(p[2])
            elif kind == "bin":
                pass
            elif kind == "par":
                current = None
    return run, res


def _num(s):
    try:
        return float(s)
    except ValueError:
        return math.nan


def parse_vec(path, wanted):
    """Reads vectors whose name matches one of the regexes in `wanted`.
    Returns list of dicts (module, name, time, value)."""
    decl = {}
    rows = []
    pats = [re.compile(w) for w in wanted]
    with open(path) as f:
        for line in f:
            if not line or line[0] in "\n":
                continue
            if line.startswith("vector "):
                p = shlex.split(line)
                vid, module, name = int(p[1]), p[2], p[3]
                cols = p[4] if len(p) > 4 else "TV"
                if any(r.search(name) for r in pats):
                    decl[vid] = (module, name, cols)
                continue
            c = line[0]
            if not c.isdigit():
                continue
            p = line.split()
            vid = int(p[0])
            if vid not in decl:
                continue
            module, name, cols = decl[vid]
            t_idx = cols.index("T") + 1
            v_idx = cols.index("V") + 1
            rows.append({"module": module, "name": name, "time": float(p[t_idx]), "value": float(p[v_idx])})
    return rows


def g(res, module_re, name):
    """All values of statistic `name` on modules matching regex."""
    r = re.compile(module_re)
    return [v for (m, n), v in res.items() if n == name and r.search(m) and not math.isnan(v)]


def weighted_mean(res, module_re, stat):
    means = {m: v for (m, n), v in res.items() if n == stat + ":mean" and re.search(module_re, m)}
    tot, w = 0.0, 0.0
    for m, v in means.items():
        c = res.get((m, stat + ":count"), 0.0)
        if c and not math.isnan(v):
            tot += v * c
            w += c
    return tot / w if w > 0 else math.nan


def derive(run, res):
    cfg = run["attrs"].get("configname", "")
    d = {
        "config": cfg,
        "runId": run.get("runId", ""),
        "repetition": int(run["attrs"].get("repetition", 0)),
        "measurement": run["attrs"].get("measurement", ""),
    }
    for k, v in run["itervars"].items():
        d[k] = v
    sim_time = _num(run["config"].get("sim-time-limit", "nan").rstrip("s")) if "sim-time-limit" in run["config"] else math.nan
    warmup = _num(run["config"].get("warmup-period", "0s").rstrip("s")) if "warmup-period" in run["config"] else 0.0
    window = (sim_time - warmup) if not math.isnan(sim_time) else MEASUREMENT_WINDOW_S
    d["measurementWindow_s"] = window

    clients = r'clientGroup\d+\.app\[0\]$'
    proxy = r'reverseProxy\.app\[0\]$'
    sat = r'satLinkController\[\d+\]$'

    users = sum(g(res, clients, "numUsers")) or math.nan
    d["numUsers"] = users
    # latency: ms
    d["responseTime_ms"] = 1e3 * weighted_mean(res, clients, "responseTime")
    d["webResponseTime_ms"] = 1e3 * weighted_mean(res, clients, "webResponseTime")
    d["videoResponseTime_ms"] = 1e3 * weighted_mean(res, clients, "videoResponseTime")
    d["hitResponseTime_ms"] = 1e3 * weighted_mean(res, clients, "hitResponseTime")
    d["missResponseTime_ms"] = 1e3 * weighted_mean(res, clients, "missResponseTime")
    sent = sum(g(res, clients, "requestSent:count"))
    dropped = sum(g(res, clients, "requestDropped:count"))
    d["requestsSent"] = sent
    d["requestsDroppedAtClient"] = dropped
    d["clientDropRatio"] = dropped / (sent + dropped) if (sent + dropped) > 0 else math.nan

    # cache
    hit_mean = g(res, proxy, "cacheHit:mean")
    d["cacheHitRatio_pct"] = 100 * hit_mean[0] if hit_mean else math.nan
    served = sum(g(res, proxy, "bytesServed:sum"))
    from_cache = sum(g(res, proxy, "bytesFromCache:sum"))
    from_origin = sum(g(res, proxy, "bytesFromOrigin:sum"))
    red_origin = sum(g(res, proxy, "redundantOriginBytes:sum"))
    red_deliv = sum(g(res, proxy, "redundantDeliveredBytes:sum"))
    d["byteHitRatio_pct"] = 100 * from_cache / served if served else math.nan
    d["bandwidthSavings_pct"] = d["byteHitRatio_pct"]
    d["redundantOriginTrafficRatio_pct"] = 100 * red_origin / from_origin if from_origin else math.nan
    d["redundantDeliveredTrafficRatio_pct"] = 100 * red_deliv / served if served else math.nan
    ot = g(res, proxy, "originThroughput:mean")
    d["originThroughput_Mbps"] = ot[0] / 1e6 if ot else math.nan

    # satellite link
    util = g(res, sat, "satDownlinkUtilization:mean")
    d["satDownlinkUtilization_pct"] = 100 * sum(util) / len(util) if util else math.nan
    uutil = g(res, sat, "satUplinkUtilization:mean")
    d["satUplinkUtilization_pct"] = 100 * sum(uutil) / len(uutil) if uutil else math.nan
    dl = g(res, sat, "satDownlinkThroughput:mean")
    d["satDownlinkThroughput_Mbps"] = sum(dl) / 1e6 if dl else math.nan
    lost = sum(g(res, sat, "satPacketsLost:sum"))
    qdrop = sum(g(res, sat, "satQueueDrops:sum"))
    psent = sum(g(res, sat, "satPacketsSent:sum"))
    d["satPacketLossRate_pct"] = 100 * (lost + qdrop) / (psent + qdrop) if (psent + qdrop) else math.nan
    d["satChannelLossRate_pct"] = 100 * lost / psent if psent else math.nan
    d["handovers"] = sum(g(res, sat, "handover:count"))

    # throughput / goodput
    body = sum(g(res, clients, "bodyBytesReceived:sum"))
    d["appThroughput_Mbps"] = body * 8 / window / 1e6 if window else math.nan
    d["throughputPerClient_Mbps"] = d["appThroughput_Mbps"] / users if users else math.nan
    d["goodputRatio_pct"] = (100 * d["appThroughput_Mbps"] / d["satDownlinkThroughput_Mbps"]
                            if d["satDownlinkThroughput_Mbps"] and not math.isnan(d["satDownlinkThroughput_Mbps"]) else math.nan)

    # DASH
    osc = sum(g(res, clients, "dashOscillation:count"))
    d["dashOscillations"] = osc
    d["dashOscillationsPerClientPerMin"] = osc / users / (window / 60.0) if users and window else math.nan
    q = g(res, clients, "dashQualityLevel:mean")
    d["dashQualityMean"] = sum(q) / len(q) if q else math.nan
    d["dashStallCount"] = sum(g(res, clients, "dashStallDuration:count"))
    d["dashStallTime_s"] = sum(g(res, clients, "dashStallDuration:sum"))
    bl = g(res, clients, "dashBufferLevel:mean")
    d["dashBufferMean_s"] = sum(bl) / len(bl) if bl else math.nan

    # DT+LRU
    acc = g(res, proxy, "dtPredictionAccuracy:mean")
    d["dtAccuracy_pct"] = 100 * acc[0] if acc else math.nan
    return d


TIMESERIES = [
    r"^groupThroughput:vector$", r"^throughputPerUser:vector$", r"^meanResponseTimeWindow:vector$",
    r"^satDownlinkThroughput:vector$", r"^satDownlinkUtilization:vector$", r"^cacheHitRatioWindow:vector$",
    r"^byteHitRatioWindow:vector$", r"^originThroughput:vector$", r"^tracedBufferLevel:vector$",
    r"^tracedQualityLevel:vector$", r"^dtPredictionAccuracy:vector$", r"^zipfAlpha:vector$",
    r"^handover:vector$",
]


def main():
    ap = argparse.ArgumentParser()
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("--results", default=os.path.join(here, "..", "simulations", "results"))
    ap.add_argument("--out", default=os.path.join(here, "csv"))
    ap.add_argument("--no-vectors", action="store_true")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    sca_files = sorted(glob.glob(os.path.join(args.results, "**", "*.sca"), recursive=True))
    if not sca_files:
        raise SystemExit("No .sca files found in " + args.results)
    rows, ts = [], []
    for f in sca_files:
        run, res = parse_sca(f)
        d = derive(run, res)
        d["file"] = os.path.basename(f)
        rows.append(d)
        vec = f[:-4] + ".vec"
        if not args.no_vectors and os.path.exists(vec):
            for r in parse_vec(vec, TIMESERIES):
                r.update({k: d.get(k) for k in ["config", "repetition"] + list(run["itervars"].keys())})
                ts.append(r)
    runs = pd.DataFrame(rows)
    runs.to_csv(os.path.join(args.out, "runs.csv"), index=False)

    is_num = pd.api.types.is_numeric_dtype
    skip = ("config", "runId", "repetition", "file", "measurement")
    group_cols = ["config"] + [c for c in runs.columns if c not in skip and not is_num(runs[c])]
    num_cols = [c for c in runs.columns if c not in skip and c not in group_cols and is_num(runs[c])]
    runs[group_cols] = runs[group_cols].fillna("")
    summary = runs.groupby(group_cols, dropna=False)[num_cols].mean().reset_index()
    summary["replications"] = runs.groupby(group_cols, dropna=False)["repetition"].count().values
    summary.to_csv(os.path.join(args.out, "summary.csv"), index=False)
    if ts:
        pd.DataFrame(ts).to_csv(os.path.join(args.out, "timeseries.csv"), index=False)
    print("runs: %d  ->  %s" % (len(runs), args.out))


if __name__ == "__main__":
    main()
