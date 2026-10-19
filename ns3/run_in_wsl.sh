#!/usr/bin/env bash
set -euo pipefail

# Optional first argument: Windows project path translated by `wslpath`.
PROJECT_DIR="${1:-/mnt/c/Users/11654/Documents/Codex/2026-09-21/sumo-ns-3-python-sumo-1}"
NS3_HOME="${NS3_HOME:-/home/songlei/ns-allinone-3.42/ns-3.42}"

test -d "$NS3_HOME" || { echo "ns-3.42 was not found at $NS3_HOME. Set NS3_HOME and retry." >&2; exit 1; }
mkdir -p "$PROJECT_DIR/data"
cp "$PROJECT_DIR/ns3/high_mobility_edge.cc" "$NS3_HOME/scratch/high_mobility_edge.cc"
cd "$NS3_HOME"
./ns3 run "scratch/high_mobility_edge --trace=$PROJECT_DIR/data/vehicle_trace.csv --rx=$PROJECT_DIR/data/received_packets.csv --flowmon=$PROJECT_DIR/data/flowmon.xml"
