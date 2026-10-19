"""Generate a SUMO mobility trace for the high-mobility edge-network study."""
from __future__ import annotations

import argparse
import csv
import os
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SUMO_DIR = ROOT / "sumo"
OUT = ROOT / "data" / "vehicle_trace.csv"


def load_traci() -> None:
    sumo_home = os.environ.get("SUMO_HOME")
    if sumo_home:
        tools = Path(sumo_home) / "tools"
        sys.path.insert(0, str(tools))
    try:
        import traci  # noqa: F401
    except ImportError as exc:
        raise SystemExit("Cannot import traci. Set SUMO_HOME or install SUMO's Python tools.") from exc


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--speed-kmh", type=float, default=60.0)
    parser.add_argument("--vehicles", type=int, default=12)
    parser.add_argument("--duration", type=float, default=60.0)
    parser.add_argument("--step", type=float, default=0.1)
    parser.add_argument("--gui", action="store_true")
    args = parser.parse_args()
    if not 0 < args.speed_kmh <= 120 or args.vehicles < 1 or args.step <= 0:
        raise SystemExit("Use speed in (0, 120], at least one vehicle, and a positive step.")

    load_traci()
    import traci

    binary = "sumo-gui" if args.gui else "sumo"
    command = [binary, "-c", str(SUMO_DIR / "scenario.sumocfg"), "--start", "--quit-on-end"]
    OUT.parent.mkdir(parents=True, exist_ok=True)
    traci.start(command)
    speed_mps = args.speed_kmh / 3.6
    try:
        with OUT.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.writer(stream)
            writer.writerow(["time_s", "vehicle_id", "x_m", "y_m", "speed_mps"])
            for index in range(args.vehicles):
                vehicle_id = f"veh{index:03d}"
                route_id = "west_to_east" if index % 2 == 0 else "east_to_west"
                traci.vehicle.add(vehicle_id, routeID=route_id, typeID="connected_car", depart=str(index * 0.5))
                traci.vehicle.setMaxSpeed(vehicle_id, speed_mps)
            while traci.simulation.getTime() < args.duration and traci.simulation.getMinExpectedNumber() > 0:
                traci.simulationStep()
                now = traci.simulation.getTime()
                for vehicle_id in traci.vehicle.getIDList():
                    x, y = traci.vehicle.getPosition(vehicle_id)
                    writer.writerow([f"{now:.3f}", vehicle_id, f"{x:.3f}", f"{y:.3f}", f"{traci.vehicle.getSpeed(vehicle_id):.3f}"])
    finally:
        traci.close()
    print(f"Wrote {OUT}")


if __name__ == "__main__":
    main()
