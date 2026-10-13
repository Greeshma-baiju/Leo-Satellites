# Evaluation of HTTP Reverse Proxies over LEO Satellite Constellations

This repository contains the OMNeT++ simulation model developed for the master's thesis *Evaluation of HTTP Reverse Proxies over LEO Satellite Constellations*. It is provided to support transparency and reproducibility of the thesis work.

The model is intended for studying HTTP reverse-proxy caching in a simplified low-Earth-orbit (LEO) satellite network. It was developed with **OMNeT++ 6.0.1** and **INET 4.4.0**; compatibility with other versions has not been verified.

## Project overview

The simulated network comprises clients, forward proxies, user terminals, three satellite nodes, ground gateways, a shared reverse proxy, and five origin servers. The configuration file defines scenarios covering:

- cache replacement policies and cache sizes,
- request workloads,
- link conditions,
- video streaming traffic, and
- changes in content popularity over time.

In the main reference configuration, the reverse proxy is located **between the ground gateways and the origin servers**. Under this placement, a cache hit can reduce traffic between the reverse proxy and the origin servers, but the response must still traverse the satellite segment to reach the client. Reductions in origin-side traffic should therefore not be interpreted as equivalent reductions in satellite-link traffic.

## Repository structure

```text
src/            Simulation model (NED files, C++ sources, message definitions)
simulations/    Simulation configurations (omnetpp.ini)
analysis/       Result-processing and plotting scripts
README.md       Project overview and usage notes
```

## Requirements

- OMNeT++ 6.0.1
- INET 4.4.0, built and available in the same workspace
- Python 3 for the analysis scripts (required packages are listed in the scripts' import statements)

## Build and run

1. Import the project into the OMNeT++ IDE.
2. Add the INET project as a project reference. The supplied configuration expects the project name `inet4.4`; adjust the reference if your INET project uses a different name.
3. Build the project.
4. Open `simulations/omnetpp.ini` and select a configuration. The `Smoke` configuration is a small scenario intended as an initial functional check, not as an experimental result.

To process results, run the analysis scripts from the `analysis/` directory after generating compatible simulation output:

```bash
cd analysis
python3 parse_results.py
python3 plot_figures.py
```

The scripts assume the result file locations and formats produced by the supplied configurations. Results from modified configurations may require changes to the scripts.

## Scope and limitations

- This model is a controlled three-satellite research testbed. It is not a complete or validated model of any operational satellite constellation, and it does not represent the behaviour of any specific commercial system.
- Several parameters that are not fixed by the thesis specification (e.g. selected link, traffic, and cache settings) are modelling assumptions. Their values and justification are documented in the thesis and in `omnetpp.ini`.
- Results depend on the chosen topology, traffic workload, cache placement, and link configuration, and should not be generalised beyond the conditions studied.
- The presence of a configuration in `omnetpp.ini` does not imply that the corresponding experiment was executed, validated, or reported in the thesis. Only results explicitly presented in the thesis should be regarded as findings of this work.
- Simulation results may vary with random seeds and number of repetitions; refer to the thesis for the statistical treatment applied.

For the full methodology, parameter choices, experimental results, and their interpretation, please refer to the thesis.


## Attribution and licensing

OMNeT++ and INET are third-party simulation frameworks distributed under their own licences; this repository does not include or redistribute them. Any external code, algorithms, or datasets used or adapted in this project should be cited according to their respective attribution requirements and applicable university policy.

[Add the licence that applies to this repository, or state that no licence is granted.]
