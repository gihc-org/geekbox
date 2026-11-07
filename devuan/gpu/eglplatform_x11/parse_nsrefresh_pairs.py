#!/usr/bin/env python3
"""Pair nsRefreshDriver allocations/completions and report per-process latency."""

from __future__ import annotations

import argparse
import re
import statistics
from collections import Counter, defaultdict
from datetime import datetime
from pathlib import Path


LINE_RE = re.compile(
    r"^(?P<time>\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+) UTC - "
    r"\[(?P<kind>Parent|Child) (?P<pid>\d+): Main Thread\]: "
    r"D/nsRefreshDriver \[(?P<driver>[0-9a-f]+)\] "
    r"(?P<event>Allocating|Completed) transaction id (?P<txid>\d+)"
)


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    return ordered[round((len(ordered) - 1) * fraction)]


def parse_timestamp(value: str) -> datetime:
    return datetime.strptime(value, "%Y-%m-%d %H:%M:%S.%f")


def parse_files(paths: list[Path]) -> tuple[list[tuple], Counter]:
    pending: dict[tuple[str, int, str, int], datetime] = {}
    samples = []
    unmatched = Counter()

    for path in sorted(paths):
        with path.open(errors="replace") as stream:
            for line in stream:
                match = LINE_RE.match(line)
                if not match:
                    continue
                fields = match.groupdict()
                key = (
                    fields["kind"],
                    int(fields["pid"]),
                    fields["driver"],
                    int(fields["txid"]),
                )
                timestamp = parse_timestamp(fields["time"])
                event = fields["event"]
                if event == "Allocating":
                    if key in pending:
                        unmatched["duplicate allocation"] += 1
                    pending[key] = timestamp
                elif key in pending:
                    samples.append((key, pending.pop(key), timestamp))
                else:
                    unmatched["completion without allocation"] += 1

    unmatched["allocation without completion"] += len(pending)
    return samples, unmatched


def describe(values: list[float]) -> str:
    return (
        f"n={len(values)} p50={statistics.median(values):.3f}s "
        f"p90={percentile(values, 0.90):.3f}s "
        f"p99={percentile(values, 0.99):.3f}s "
        f"min={min(values):.3f}s max={max(values):.3f}s"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=Path)
    parser.add_argument(
        "--minute",
        action="store_true",
        help="vis completion-rate og latency pr. UTC-minut",
    )
    args = parser.parse_args()

    samples, unmatched = parse_files(args.logs)
    by_process: dict[tuple[str, int], list[tuple]] = defaultdict(list)
    for sample in samples:
        kind, pid = sample[0][0], sample[0][1]
        by_process[(kind, pid)].append(sample)

    print(f"matched pairs: {len(samples)}")
    for (kind, pid), values in sorted(by_process.items()):
        durations = [(completed - allocated).total_seconds()
                     for _, allocated, completed in values]
        print(f"{kind} {pid}: {describe(durations)}")

    for reason, count in sorted(unmatched.items()):
        print(f"unmatched {reason}: {count}")

    if args.minute:
        by_minute: dict[str, list[tuple]] = defaultdict(list)
        for sample in samples:
            by_minute[sample[2].strftime("%Y-%m-%d %H:%M")].append(sample)
        print()
        print("UTC minute  completions/s  latency")
        for minute, values in sorted(by_minute.items()):
            durations = [(completed - allocated).total_seconds()
                         for _, allocated, completed in values]
            print(
                f"{minute}  {len(values) / 60:12.3f}  "
                f"p50={statistics.median(durations):.3f}s "
                f"p90={percentile(durations, 0.90):.3f}s"
            )


if __name__ == "__main__":
    main()
