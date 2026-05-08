#!/usr/bin/env python3
"""
Generate sample 2D points for the C kmeans app.

The output format matches BE-apps/kmeans conventions:
  x,y
  1.234,-5.678
"""

import argparse
import csv
import random
from pathlib import Path


SCRIPT_DIR = Path(__file__).parent
OUTPUT_PATH = SCRIPT_DIR / "points.csv"


def generate_points(n_samples: int, n_clusters: int, seed: int, stddev: float) -> list[tuple[float, float]]:
    rng = random.Random(seed)

    centers: list[tuple[float, float]] = []
    for _ in range(n_clusters):
        centers.append((rng.uniform(-10.0, 10.0), rng.uniform(-10.0, 10.0)))

    points: list[tuple[float, float]] = []
    base = n_samples // n_clusters
    remainder = n_samples % n_clusters

    for idx, (cx, cy) in enumerate(centers):
        count = base + (1 if idx < remainder else 0)
        for _ in range(count):
            points.append((rng.gauss(cx, stddev), rng.gauss(cy, stddev)))

    return points


def write_csv(points: list[tuple[float, float]], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as fp:
        writer = csv.writer(fp)
        writer.writerow(["x", "y"])
        for x, y in points:
            writer.writerow([f"{x:.6f}", f"{y:.6f}"])


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate data/points.csv for C kmeans.")
    parser.add_argument("--samples", type=int, default=300, help="Total number of points.")
    parser.add_argument("--clusters", type=int, default=4, help="Number of cluster centers.")
    parser.add_argument("--seed", type=int, default=42, help="Random seed.")
    parser.add_argument("--stddev", type=float, default=1.5, help="Cluster spread.")
    parser.add_argument("--output", type=Path, default=OUTPUT_PATH, help="Output CSV path.")
    args = parser.parse_args()

    points = generate_points(args.samples, args.clusters, args.seed, args.stddev)
    write_csv(points, args.output)
    print(f"wrote {len(points)} points to {args.output}")


if __name__ == "__main__":
    main()
