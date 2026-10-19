#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="${1:?Usage: bash run_socket_in_wsl.sh <project-dir> <windows-host-ip> [port]}"
WINDOWS_HOST="${2:-$(ip route | awk '/default/ {print $3; exit}') }"
WINDOWS_HOST="${WINDOWS_HOST// /}"
CONTROL_PORT="${3:-50007}"
NS3_HOME="${NS3_HOME:-/home/songlei/ns-allinone-3.42/ns-3.42}"

test -d "$NS3_HOME" || { echo "ns-3.42 was not found at $NS3_HOME" >&2; exit 1; }
test -n "$WINDOWS_HOST" || { echo "Cannot determine the Windows gateway IP; pass it as argument 2." >&2; exit 1; }
mkdir -p "$PROJECT_DIR/data"
cp "$PROJECT_DIR/ns3/high_mobility_socket.cc" "$NS3_HOME/scratch/high_mobility_socket.cc"
cd "$NS3_HOME"
./ns3 run "scratch/high_mobility_socket --host=$WINDOWS_HOST --controlPort=$CONTROL_PORT --packetLog=$PROJECT_DIR/data/v2i_packet_log.csv"
