#!/usr/bin/env python3
"""
Produces the result figures/tables from the CSV files written by
parse_results.py (means over the three replications).

Usage:
    python3 parse_results.py          # once, after the simulations
    python3 plot_figures.py [--csv csv] [--out figures]

Every figure functions configuration.
"""
import argparse
import math
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

ARGS = None


def load():
    s = pd.read_csv(os.path.join(ARGS.csv, "summary.csv"))
    ts_path = os.path.join(ARGS.csv, "timeseries.csv")
    ts = pd.read_csv(ts_path) if os.path.exists(ts_path) else pd.DataFrame()
    for df in (s, ts):
        for c in ("clients", "cacheGB", "capacityMbps", "rate"):
            if c in df.columns:
                df[c] = pd.to_numeric(df[c], errors="coerce")
        for c in ("alpha", "loss"):
            if c in df.columns:
                df[c] = df[c].astype(str)
    return s, ts


def cache_label(row):
    if "cacheGB" in row and pd.notna(row["cacheGB"]) and row["cacheGB"] == 0:
        return "No cache"
    pol = row.get("policy", "LRU")
    pol = "LRU" if pd.isna(pol) else pol
    gb = row.get("cacheGB")
    return "%s %d GB" % (pol, gb) if pd.notna(gb) else pol


def title_of(key):
    if isinstance(key, tuple):
        return ", ".join(str(k) for k in key)
    return str(key)


def save(fig, name):
    os.makedirs(ARGS.out, exist_ok=True)
    fig.tight_layout()
    fig.savefig(os.path.join(ARGS.out, name + ".pdf"))
    fig.savefig(os.path.join(ARGS.out, name + ".png"), dpi=150)
    plt.close(fig)
    print("wrote", name)


def panels(df, panel_cols, x, y, series, title, ylabel, xlabel, name, kind="line"):
    if df.empty:
        return
    groups = list(df.groupby(panel_cols)) if panel_cols else [("all", df)]
    n = len(groups)
    cols = min(3, n)
    rows = (n + cols - 1) // cols
    fig, axes = plt.subplots(rows, cols, figsize=(5 * cols, 3.8 * rows), squeeze=False)
    for ax, (key, sub) in zip(axes.flat, groups):
        for lab, ss in sub.groupby(series):
            lab = title_of(lab)
            ss = ss.sort_values(x)
            if kind == "scatter":
                ax.scatter(ss[x], ss[y], label=str(lab), s=18)
            else:
                ax.plot(ss[x], ss[y], marker="o", label=str(lab))
        ax.set_title(title_of(key))
        ax.set_xlabel(xlabel)
        ax.set_ylabel(ylabel)
        ax.grid(True, alpha=0.3)
        ax.legend(fontsize=7)
    for ax in list(axes.flat)[n:]:
        ax.axis("off")
    fig.suptitle(title)
    save(fig, name)


def cfg(s, name):
    d = s[s["config"] == name].copy()
    if not d.empty:
        d["cache"] = d.apply(cache_label, axis=1)
    return d


def latency_vs_flows(s):
    d = cfg(s, "LatencyVsFlows")
    if d.empty:
        return
    panels(d[d.traffic == "video"], ["scenario"], "clients", "videoResponseTime_ms", "cache",
           "Response time vs concurrent flows (video)", "Average response time (ms)", "Concurrent flows", "latency_vs_flows_video")
    panels(d[d.traffic == "web"], ["scenario"], "clients", "webResponseTime_ms", "cache",
           "Response time vs concurrent flows (web)", "Average response time (ms)", "Concurrent flows", "latency_vs_flows_web")
    panels(d, ["traffic", "scenario"], "clients", "throughputPerClient_Mbps", "cache",
           "Throughput per client vs flows", "Throughput per client (Mbit/s)", "Concurrent flows", "throughput_vs_flows")


def hit_ratio_vs_cache_size(s):
    d = cfg(s, "HitRatioVsCacheSize")
    if d.empty:
        return
    d["series"] = d["policy"] + " a=" + d["alpha"]
    base = d[d.cacheGB == 0]
    rows = [d[d.cacheGB > 0]]
    for pol in ("LRU", "LFU", "FIFO", "Random"):   # the 0 GB baseline belongs to every policy curve
        b = base.copy()
        b["policy"] = pol
        b["series"] = pol + " a=" + b["alpha"]
        rows.append(b)
    dd = pd.concat(rows)
    panels(dd, ["traffic", "scenario"], "cacheGB", "cacheHitRatio_pct", "series",
           "Cache hit ratio vs cache size", "Cache hit ratio (%)", "Cache size (GB)", "hit_ratio_vs_cache_size")
    for tr, fig in (("video", "latency_vs_hit_ratio_video"), ("web", "latency_vs_hit_ratio_web")):
        x = d[d.traffic == tr]
        y = "videoResponseTime_ms" if tr == "video" else "webResponseTime_ms"
        panels(x, [], "cacheHitRatio_pct", y, "scenario",
               "Response time vs cache hit ratio (%s)" % tr, "Average response time (ms)", "Cache hit ratio (%)", fig, kind="scatter")
    t = d[(d.alpha == "1.0") & (d.cacheGB == 10)]
    if not t.empty:
        table = t.pivot_table(index=["traffic"], columns="policy", values="cacheHitRatio_pct", aggfunc="mean")
        util = t.groupby("policy")["satDownlinkUtilization_pct"].mean()
        orig = t.groupby("policy")["originThroughput_Mbps"].mean()
        os.makedirs(ARGS.out, exist_ok=True)
        out = table.copy()
        out.loc["satLinkUtilization_pct"] = util
        out.loc["originThroughput_Mbps"] = orig
        out.to_csv(os.path.join(ARGS.out, "policy_comparison.csv"))
        fig, ax = plt.subplots(figsize=(6, 4))
        table.T.plot(kind="bar", ax=ax)
        ax.set_ylabel("Cache hit ratio (%)")
        ax.set_title("Hit ratio by policy (alpha = 1.0, 10 GB)")
        save(fig, "hit_ratio_by_policy")


def utilization_vs_flows(s):
    d = cfg(s, "UtilizationVsFlows")
    if d.empty:
        return
    for sc, name in (("remote", "utilization_vs_flows_remote"), ("land", "utilization_vs_flows_land")):
        x = d[d.scenario == sc]
        panels(x, ["traffic", "capacityMbps"], "clients", "satDownlinkUtilization_pct", "cache",
               "Satellite link utilisation vs flows (%s)" % sc, "Link utilisation (%)", "Concurrent flows", name)


def link_capacity_vs_cache_size(s):
    d = cfg(s, "LinkCapacityVsCacheSize")
    if d.empty:
        return
    base = d[d.cacheGB == 0]
    rows = [d[d.cacheGB > 0]]
    for pol in ("LRU", "LFU", "FIFO", "Random"):
        b = base.copy()
        b["policy"] = pol
        rows.append(b)
    dd = pd.concat(rows)
    lru = dd[dd.policy == "LRU"]
    panels(lru, ["traffic"], "cacheGB", "satDownlinkUtilization_pct", "capacityMbps",
           "Satellite utilisation vs cache size (remote, LRU)", "Link utilisation (%)", "Cache size (GB)", "utilization_vs_cache_size")
    panels(lru, ["traffic"], "cacheGB", "originThroughput_Mbps", "capacityMbps",
           "Origin (backhaul) throughput vs cache size (remote, LRU)", "Origin throughput (Mbit/s)", "Cache size (GB)", "origin_throughput_vs_cache_size")
    t = lru[lru.traffic == "video"].pivot_table(index="cacheGB", columns="capacityMbps", values="satDownlinkUtilization_pct")
    os.makedirs(ARGS.out, exist_ok=True)
    t.to_csv(os.path.join(ARGS.out, "utilization_by_capacity.csv"))
    panels(dd, ["traffic", "capacityMbps"], "cacheGB", "bandwidthSavings_pct", "policy",
           "Bandwidth savings (byte hit ratio) vs cache size", "Bandwidth savings (%)", "Cache size (GB)", "bandwidth_savings_vs_cache_size")
    panels(dd, ["traffic", "capacityMbps"], "cacheGB", "redundantOriginTrafficRatio_pct", "policy",
           "Redundant traffic ratio vs cache size", "Redundant traffic (%)", "Cache size (GB)", "redundant_traffic_vs_cache_size")


def request_rate(s):
    d = cfg(s, "RequestRate")
    panels(d, ["traffic", "scenario"], "rate", "responseTime_ms", "cache",
           "Response time vs request rate", "Average response time (ms)", "Requests/s per client", "latency_vs_request_rate")


def packet_loss_vs_flows(s):
    d = cfg(s, "PacketLossVsFlows")
    if d.empty:
        return
    d["series"] = d["cache"] + ", loss=" + d["loss"]
    panels(d[d.traffic == "web"], ["scenario"], "clients", "satPacketLossRate_pct", "series",
           "Packet loss vs flows (web)", "Packet loss (%)", "Concurrent flows", "packet_loss_vs_flows_web")
    panels(d[d.traffic == "video"], ["scenario"], "clients", "satPacketLossRate_pct", "series",
           "Packet loss vs flows (video)", "Packet loss (%)", "Concurrent flows", "packet_loss_vs_flows_video")


def ts_plot(ts, config, name_vec, filt, series_cols, agg, title, ylabel, fname, scale=1.0, panel_col=None,
            module=None, bin_s=1.0):
    """Time series of a vector. Samples of different modules are aligned to
    bins of bin_s seconds (the client groups start at slightly different
    times) and combined with `agg` (sum for throughput, mean otherwise)."""
    d = ts[(ts["config"] == config) & (ts["name"] == name_vec)].copy()
    if module is not None:
        d = d[d["module"].str.contains(module, regex=False)]
    if d.empty:
        return
    d["time"] = (d["time"] / bin_s).apply(math.floor) * bin_s
    for k, v in filt.items():
        if k not in d.columns:
            return
        d = d[d[k] == v]
    if d.empty:
        return
    d = d[d["repetition"] == d["repetition"].min()]   # one representative replication
    keys = [c for c in series_cols if c in d.columns]
    pcols = [panel_col] if panel_col and panel_col in d.columns else []
    # average each module within a bin first, then combine modules with `agg`
    d = d.groupby(keys + pcols + ["module", "time"], dropna=False)["value"].mean().reset_index()
    groups = list(d.groupby(pcols)) if pcols else [("", d)]
    fig, axes = plt.subplots(1, len(groups), figsize=(6 * len(groups), 4), squeeze=False)
    for ax, (pk, sub) in zip(axes.flat, groups):
        for lab, ss in sub.groupby(keys) if keys else [("", sub)]:
            series = ss.groupby("time")["value"].agg(agg).sort_index() * scale
            ax.plot(series.index, series.values, label=title_of(lab), lw=1)
        ax.set_title(title_of(pk))
        ax.set_xlabel("Simulation time (s)")
        ax.set_ylabel(ylabel)
        ax.grid(True, alpha=0.3)
        ax.legend(fontsize=7)
    fig.suptitle(title)
    save(fig, fname)


def label_ts(ts):
    if ts.empty:
        return ts
    if "cacheGB" in ts.columns:
        ts["cache"] = ts.apply(cache_label, axis=1)
    return ts


def throughput_over_time(ts):
    for sc, name in (("land", "throughput_over_time_land"), ("remote", "throughput_over_time_remote")):
        ts_plot(ts, "ThroughputOverTime", "groupThroughput:vector", {"scenario": sc}, ["cache"], "sum",
                "Throughput vs time (%s)" % sc, "Client throughput (Mbit/s)", name, scale=1e-6, bin_s=1)


def dash_oscillation(s):
    d = s[s["config"] == "DASH_CacheAwareABR"]
    if d.empty:
        return
    p = d.pivot_table(index=["scenario", "alpha"], columns="cacheAwareABR", values="dashOscillationsPerClientPerMin")
    if "false" in p.columns and "true" in p.columns:
        p["reduction_pct"] = 100 * (p["false"] - p["true"]) / p["false"]
    os.makedirs(ARGS.out, exist_ok=True)
    p.to_csv(os.path.join(ARGS.out, "dash_oscillation.csv"))
    print("wrote dash_oscillation.csv")


def dash_traces(ts):
    # one traced DASH client (user 0 of client group 0)
    ts_plot(ts, "DASH_CacheAwareABR", "tracedQualityLevel:vector", {"alpha": "1.0"}, ["cacheAwareABR"], "mean",
            "Quality level of a traced client", "Representation index", "dash_quality_trace", panel_col="scenario",
            module="clientGroup1.", bin_s=1e-6)
    for sc, name in (("land", "dash_buffer_land"), ("remote", "dash_buffer_remote")):
        ts_plot(ts, "DASH_BufferDynamics", "tracedBufferLevel:vector", {"scenario": sc}, ["cache"], "mean",
                "Buffer vs time (%s)" % sc, "Buffer level (s)", name, module="clientGroup1.", bin_s=1e-6)


def dynamic_popularity(ts):
    c = "DynamicPopularity"
    ts_plot(ts, c, "meanResponseTimeWindow:vector", {}, ["policy"], "mean",
            "Response time vs time (remote)", "Average response time (ms)", "dynamic_response_time", scale=1e3, panel_col="traffic", bin_s=30)
    ts_plot(ts, c, "dtPredictionAccuracy:vector", {"policy": "DTLRU"}, ["policy"], "mean",
            "DT prediction accuracy", "Accuracy (%)", "dt_accuracy", scale=100, panel_col="traffic")
    ts_plot(ts, c, "originThroughput:vector", {}, ["policy"], "sum",
            "Origin/backhaul throughput vs time", "Throughput (Mbit/s)", "dynamic_origin_throughput", scale=1e-6, panel_col="traffic", bin_s=10)
    ts_plot(ts, c, "satDownlinkThroughput:vector", {}, ["policy"], "sum",
            "Satellite downlink throughput vs time", "Throughput (Mbit/s)", "dynamic_satellite_throughput", scale=1e-6, panel_col="traffic", bin_s=10)
    ts_plot(ts, c, "cacheHitRatioWindow:vector", {}, ["policy"], "mean",
            "Cache hit ratio vs time", "Cache hit ratio (%)", "dynamic_hit_ratio", scale=100, panel_col="traffic", bin_s=10)


def main():
    global ARGS
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", default=os.path.join(here, "csv"))
    ap.add_argument("--out", default=os.path.join(here, "figures"))
    ARGS = ap.parse_args()
    s, ts = load()
    ts = label_ts(ts)
    for f in (latency_vs_flows, hit_ratio_vs_cache_size, utilization_vs_flows, link_capacity_vs_cache_size,
              request_rate, packet_loss_vs_flows, dash_oscillation):
        f(s)
    if not ts.empty:
        for f in (throughput_over_time, dash_traces, dynamic_popularity):
            f(ts)


if __name__ == "__main__":
    main()
