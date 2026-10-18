#!/bin/bash
# Task 3 driver: builds node.c / base.c, runs grid and line experiments with
# an MPMD launch (nodes on ranks 0..m*n-1, base station on the last rank),
# then plots the delay graphs.
# Usage: ./run_analysis.sh [sim_seconds=20]
set -e
SIM=${1:-20}

mpicc -fopenmp -O2 -Wall -o ev_node node.c
mpicc -O2 -Wall -o ev_base base.c
rm -f analysis.csv

# run m n : args are  m n k sim_s cycle_ms thr period_s busy_prob
run() {
  local m=$1 n=$2 args
  args="$m $n 6 $SIM 500 2 3 0.8"
  echo "=== ${m}x${n} ==="
  mpirun --oversubscribe -np $((m*n)) ./ev_node $args : -np 1 ./ev_base $args
}

# (a) square grids
for s in 2 3 4 5; do run $s $s; done
# (b) line topology
for n in 2 4 6 8 12 16; do run 1 $n; done

python3 - <<'PY'
import csv
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
rows = list(csv.DictReader(open("analysis.csv")))
types = sorted({r["msg_type"] for r in rows})
for topo, label in (("grid", "EV grid (m x m)"), ("line", "line topology (1 x n)")):
    plt.figure(figsize=(7, 4.5))
    for t in types:
        pts = sorted((int(r["nodes"]), float(r["avg_delay_us"])) for r in rows
                     if r["topology"] == topo and r["msg_type"] == t)
        if pts:
            plt.plot(*zip(*pts), marker="o", label=t)
    plt.xlabel("number of EV charging nodes")
    plt.ylabel("average one-way delay (us)")
    plt.title("Communication delay vs size: " + label)
    plt.legend(fontsize=8); plt.grid(True); plt.tight_layout()
    plt.savefig(f"delay_{topo}.png", dpi=150)
print("Saved delay_grid.png and delay_line.png")
PY
