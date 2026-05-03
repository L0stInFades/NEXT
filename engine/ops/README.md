# NEXT Ops Module

`next_ops` owns the engine-side scaffolding for real-code gameplay systems that
are not specific to a single game target.

The first implemented piece is `OpsWorkspace`: a controlled player workspace
for the HackOps maintenance-window prototype. It can generate seed files,
reset the workspace, list files, and create snapshots without exposing the
entire host filesystem as game state.

Current seed files:

- `ops.toml`
- `policy.py`
- `orders.log`
- `city_graph.json`
- `README.md`

This module does not execute player code yet. `PythonWorker` and `WorldApi`
should build on top of this workspace boundary.
