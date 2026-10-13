# Implementing the project in the OMNeT++ 6.0.1 IDE

Step-by-step guide to importing, building, running and analysing the
`Evaluation_of_HTTP_Reverse_Proxies_over_LEO_Satellite_Constellations`
project with OMNeT++ 6.0.1 and INET 4.4.0.

---

## 1. Install OMNeT++ 6.0.1 (compatible: OMNeT++ 6.0.3, 6.0.2 and INET 4.3.0, 4.5.0 )

**Linux**
```bash
tar xzf omnetpp-6.0.1-linux-x86_64.tgz
cd omnetpp-6.0.1
source setenv
./configure
make -j$(nproc)
omnetpp            # starts the IDE
```

**Windows**
1. Unpack `omnetpp-6.0.1-windows-x86_64.zip` to a path **without spaces**, e.g. `C:\omnetpp-6.0.1`.
2. Run `mingwenv.cmd`. In the terminal that opens, run `./configure && make -j8`.
3. Start the IDE with `omnetpp`.

When the IDE asks for a workspace, choose a folder without spaces, e.g.
`~/omnetpp-6.0.1/samples` or `C:\omnetpp-ws`.

---

## 2. Install and build INET 4.4.0

1. **Help > Install Simulation Models...**, choose **INET Framework 4.4.0**, then **Finish**.
   (Offline alternative: download `inet-4.4.0-src.tgz` and use
   **File > Import > General > Existing Projects into Workspace > Select archive file**.)
2. The project appears as **inet4.4** in the Project Explorer.
3. Right-click **inet4.4 > Build Project**. The first build takes 20-60 minutes.
   Wait until the Console shows `Creating shared library: ../out/.../libINET.so`
   (`INET.dll` on Windows).
4. Optional check: run any INET example, e.g. `inet4.4/examples/inet/tcpclientserver`.

Build mode: the IDE builds **debug** by default. Use the same mode for INET and
for this project (toolbar hammer icon dropdown > *release* or *debug*).
Release is about 3-5 times faster for long simulations.

---

## 3. Import this project

1. **File > Import... > General > Existing Projects into Workspace > Next**.
2. Choose **Select archive file** and pick
   `Evaluation_of_HTTP_Reverse_Proxies_over_LEO_Satellite_Constellations`
   (or **Select root directory** if you have already unpacked it).
3. Tick the project, tick **Copy projects into workspace**, then **Finish**.

You will see:

```
Evaluation_of_HTTP_Reverse_Proxies_over_LEO_Satellite_Constellations
 ├─ src/          NED + C++ model (cache, common, dash, handover, networks, node, popularity, satchannel)
 ├─ simulations/  omnetpp.ini
 ├─ analysis/     parse_results.py, plot_figures.py
 ├─ README.md     model description, assumptions, statistics
 └─ IDE_GUIDE.md  this file
```


---

## 4. Set the project reference to INET

1. Right-click the project > **Properties > Project References**.
2. Tick **inet4.4** (only INET), then **Apply and Close**.

If your INET project has a different name (e.g. `inet`), tick that one instead.

Also check **Properties > OMNeT++ > NED Source Folders**: `src` and
`simulations` must be ticked. They are preset by the project's `.nedfolders`.

---

## 5. Build the project

1. Right-click the project > **Build Project** (or Ctrl+B).
2. The IDE generates `src/Makefile` from `.oppbuildspec`, runs `opp_msgc` on
   `src/common/HttpMessages.msg` and compiles all `.cc` files.
3. A successful build ends with
   `Creating shared library: ../out/<toolchain>-<mode>/src/libleoproxy.so`
   (`leoproxy.dll` on Windows; `libleoproxy_dbg.so` in debug mode).

The **Problems** view must show no errors. If the build fails, see Troubleshooting below.

---

## 6. Run a simulation in the GUI (Qtenv)

1. Open `simulations/omnetpp.ini`. The INI editor shows each configuration in its *Sections* tab.
2. Right-click `omnetpp.ini` > **Run As > 1 OMNeT++ Simulation**.
3. In the dialog, choose a configuration. Start with **Smoke** (30 clients, 120 s),
   then try **SmokeDash** (DASH + DT+LRU).
4. In Qtenv:
   - Double-click a node (e.g. `reverseProxy`) to open it, and its `app[0]` to see the cache counters.
   - Use **Run** (F5) for animated runs, or **Express run** (F7) for full speed.
   - When the run ends, click **Conclude simulation** so that `finish()` writes the scalars.

The IDE adds the libraries (`INET`, `leoproxy`) and the NED path automatically
from the project references.

---

## 7. Run experiments in batch (Cmdenv)

The experiments have hundreds of runs, so run them without the GUI.

1. **Run > Run Configurations... > OMNeT++ Simulation > New**.
2. **Main** tab:
   - Simulation: `simulations/omnetpp.ini`
   - Config name: e.g. `LatencyVsFlows`
   - Runs: all (leave empty) or a subset, e.g. `0..20` or the filter `$clients==500 && $scenario=="remote"`
   - User interface: **Command line (Cmdenv)**
   - **Allow multiple processes**: set the number of CPU cores to use
3. **Run**. Progress appears in the Console view. Each run writes
   `simulations/results/<Config>-<iteration variables>-#<repetition>.sca/.vec`.

| Config | Purpose | Runs |
|---|---|---|
| `Smoke`, `SmokeDash` | quick functional tests | 1 each |
| `Reference_NoCache`, `Reference_LRU_10GB` | baseline and default caching configuration | 3 each |
| `LatencyVsFlows` | response time and throughput vs concurrent flows | 420 |
| `HitRatioVsCacheSize` | hit ratio vs cache size, policy and Zipf alpha | 468 |
| `UtilizationVsFlows` | satellite utilisation vs flows and link capacity | 900 |
| `LinkCapacityVsCacheSize` | utilisation, bandwidth savings, redundancy vs cache size (remote) | 234 |
| `RequestRate` | request rate 1/5/10 req/s | 72 |
| `PacketLossVsFlows` | packet loss vs flows for 0/0.1/1 % | 360 |
| `ThroughputOverTime` | throughput time series | 30 |
| `DASH_Base`, `DASH_CacheAwareABR`, `DASH_BufferDynamics` | DASH streaming, oscillation, buffer traces | 3 / 36 / 30 |
| `DynamicPopularity` | four-phase Zipf, LRU vs DT+LRU | 12 |
| `Sensitivity_HandoverOutage`, `Topology_ThreeClientGroups`, `Sensitivity_UserSideCache` | additional variants | 6 / 3 / 9 |


### 7.1 Equivalent command line (optional)
```bash
cd Evaluation_of_HTTP_Reverse_Proxies_over_LEO_Satellite_Constellations/simulations
INET=../../inet4.4
opp_runall -j8 opp_run -u Cmdenv -c LatencyVsFlows -r '$clients==500' \
    -l $INET/src/INET -l ../src/leoproxy -n ../src:$INET/src omnetpp.ini
```


---

## 10. Troubleshooting

| Symptom | Fix |
|---|---|
| `Cannot load library ... libINET` / `INET_dbg` | Build `inet4.4` in the **same mode** (debug/release) as this project. |
| `No such NED type: inet.node.inet.Router` | Project reference to `inet4.4` is missing (step 4). |
| `No such NED type: leoproxy...` | `src` is not a NED source folder (step 4) or the project was not built. |
| `make: *** No rule to make target` / paths with spaces | Workspace or project path contains spaces; move it. |
| `fatal error: inet/...h: No such file` | Project reference missing, then **Project > Clean** and build again. |
| `HttpMessages_m.h: No such file` | **Project > Clean...**, then build (the message compiler runs first). |
| Simulation very slow | Use release mode, Cmdenv and express mode; reduce `*.numClients` or `sim-time-limit` for tests. |
| Run stops at `t=0` with "Unknown configuration" | The config name in Run Configurations does not match a `[Config ...]` in `omnetpp.ini`. |
| No `.sca` scalars after a GUI run | Click **Conclude simulation** (or let it reach the time limit) before closing Qtenv. |
