#!/usr/bin/env python3
import argparse
import random


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate integer dataset for sort app")
    parser.add_argument("--output", required=True, help="Output file path")
    parser.add_argument("--count", type=int, default=10000, help="Number of integers to generate")
    parser.add_argument("--seed", type=int, default=42, help="Random seed")
    parser.add_argument("--max-value", type=int, default=1000000, help="Maximum generated value")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.count <= 0:
        raise SystemExit("--count must be > 0")
    if args.max_value < 0:
        raise SystemExit("--max-value must be >= 0")

    random.seed(args.seed)
    with open(args.output, "w", encoding="utf-8") as fp:
        for _ in range(args.count):
            value = random.randint(0, args.max_value)
            fp.write(f"{value}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
