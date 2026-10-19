"""Windows-side TraCI server for the real-time SUMO - ns-3 A2 closed loop."""
from __future__ import annotations

import argparse
import csv
import socket
from pathlib import Path

from run_sumo_trace import ROOT, SUMO_DIR, load_traci


def recv_line(connection: socket.socket) -> str:
    data = bytearray()
    while not data.endswith(b"\n"):
        chunk = connection.recv(256)
        if not chunk:
            raise ConnectionError("ns-3 closed the control connection")
        data.extend(chunk)
    return data.decode("utf-8").strip()


def main() -> None:
    parser = argparse.ArgumentParser(description="TraCI Socket server for A2.3/A2.4")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=50007)
    parser.add_argument("--duration", type=float, default=60.0)
    parser.add_argument("--speed-kmh", type=float, default=60.0)
    args = parser.parse_args()
    if not 0 < args.speed_kmh <= 120 or args.duration <= 0:
        raise SystemExit("Use a positive duration and speed in (0, 120].")

    load_traci()
    import traci

    trace_path = ROOT / "data" / "socket_vehicle_trace.csv"
    trace_path.parent.mkdir(parents=True, exist_ok=True)
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind((args.host, args.port))
    listener.listen(1)
    print(f"Waiting for ns-3 at {args.host}:{args.port} ...", flush=True)

    traci.start(["sumo", "-c", str(SUMO_DIR / "scenario.sumocfg"), "--start", "--quit-on-end"])
    traci.vehicle.add("veh000", routeID="west_to_east", typeID="connected_car", depart="0")
    traci.vehicle.setMaxSpeed("veh000", args.speed_kmh / 3.6)
    try:
        connection, peer = listener.accept()
        print(f"ns-3 connected from {peer[0]}:{peer[1]}", flush=True)
        with connection, trace_path.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.writer(stream)
            writer.writerow(["time_s", "vehicle_id", "x_m", "y_m", "speed_mps"])
            while True:
                request = recv_line(connection)
                if request == "QUIT":
                    break
                parts = request.split()
                if len(parts) != 2 or parts[0] != "STEP":
                    connection.sendall(b"ERROR expected: STEP <time_s>\n")
                    continue
                requested_time = min(float(parts[1]), args.duration)
                traci.simulationStep(requested_time)
                now = traci.simulation.getTime()
                active = "veh000" in traci.vehicle.getIDList() and now <= args.duration
                if active:
                    x, y = traci.vehicle.getPosition("veh000")
                    speed = traci.vehicle.getSpeed("veh000")
                    writer.writerow([f"{now:.3f}", "veh000", f"{x:.3f}", f"{y:.3f}", f"{speed:.3f}"])
                    connection.sendall(f"POS {now:.3f} {x:.3f} {y:.3f} {speed:.3f} 1\n".encode())
                else:
                    connection.sendall(f"POS {now:.3f} 0 0 0 0\n".encode())
                    break
    finally:
        traci.close()
        listener.close()
    print(f"Wrote {trace_path}")


if __name__ == "__main__":
    main()
