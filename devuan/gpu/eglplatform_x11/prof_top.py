#!/usr/bin/env python3
"""prof_top.py — top-funktioner pr. traad i en Gecko-profiler-JSON (S2-sporet).

Bruges paa den JSON som Firefox skriver med
MOZ_PROFILER_STARTUP=1 + MOZ_PROFILER_SHUTDOWN=/tmp/prof.json (se
s1_variants.sh's "prof"-tilstand). Profilen er den *raa* form:
processes/threads -> samples + stackTable + frameTable + funcTable + stringArray.

  python3 prof_top.py /tmp/prof.json [--top 12] [--thread GeckoMain]

Uden --thread vises alle traade med mindst 20 samples, sorteret efter antal.
"""
import argparse
import collections
import json
import sys


def threads_of(profile):
    """Giver (processType, thread) for baade den gamle (top-level threads) og
    den nye (processes -> threads) form."""
    if profile.get("processes"):
        for proc in profile["processes"]:
            ptype = proc.get("type") or proc.get("processType") or "?"
            for th in proc.get("threads", []):
                yield ptype, th
    for th in profile.get("threads", []):
        yield (th.get("processType") or "?"), th


def leaf_counts(thread, libs=()):
    """Antal samples pr. leaf-funktion (ms ved 1 ms sampling)."""
    samples = thread.get("samples") or {}
    stack = thread.get("stackTable") or {}
    frame = thread.get("frameTable") or {}
    func = thread.get("funcTable") or {}
    # v31 bruger "stringTable" (ældre versioner "stringArray")
    strings = thread.get("stringArray") or thread.get("stringTable") or []

    def col(table, name):
        schema = table.get("schema") or {}
        return schema.get(name)

    def resolve(name):
        """Rå adresser ("0x…") mappes til den bibliotek de ligger i."""
        if not name.startswith("0x"):
            return name
        try:
            addr = int(name, 16)
        except ValueError:
            return name
        for start, end, lib in libs:
            if start <= addr < end:
                return "%s+0x%x" % (lib, addr - start)
        return name

    s_stack = col(stack, "frame")   # stackTable: {prefix, frame}
    # I profil-format v31 er der INGEN funcTable: frameTable's "location"-kolonne
    # indekserer direkte i trådens stringTable (målt 25. sep 2026). Ældre
    # versioner havde "func" -> funcTable -> stringArray.
    f_func = col(frame, "func")
    f_loc = col(frame, "location")
    u_name = col(func, "name")
    s_data = stack.get("data") or []
    f_data = frame.get("data") or []
    u_data = func.get("data") or []
    smp = samples.get("data") or []
    smp_stack = col(samples, "stack")

    def func_name(fidx):
        if fidx is None or fidx >= len(f_data):
            return "?"
        frow = f_data[fidx]
        if f_func is not None and f_func < len(frow) and u_data:
            fid = frow[f_func]
            if fid < len(u_data):
                urow = u_data[fid]
                if u_name is not None and u_name < len(urow):
                    nidx = urow[u_name]
                    if 0 <= nidx < len(strings):
                        return strings[nidx]
        if f_loc is not None and f_loc < len(frow):
            nidx = frow[f_loc]
            if isinstance(nidx, int) and 0 <= nidx < len(strings):
                return resolve(strings[nidx])
        return "?"

    counts = collections.Counter()
    total = 0
    for row in smp:
        total += 1
        if isinstance(row, dict):
            sid = row.get("stack")
        elif smp_stack is not None and isinstance(row, (list, tuple)) \
                and smp_stack < len(row):
            sid = row[smp_stack]
        else:
            sid = None
        if sid is None or sid >= len(s_data):
            counts["<no stack>"] += 1
            continue
        frow = s_data[sid]
        fidx = frow[s_stack] if (s_stack is not None and s_stack < len(frow)) else None
        counts[func_name(fidx)] += 1
    return counts, total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("profil")
    ap.add_argument("--top", type=int, default=12)
    ap.add_argument("--thread", default=None)
    args = ap.parse_args()

    with open(args.profil) as fh:
        profile = json.load(fh)

    meta = profile.get("meta") or {}
    print("profil: %s  (interval %.2f ms, version %s)"
          % (args.profil, meta.get("interval", 1.0), meta.get("version", "?")))

    # Adresse-frames ("0x…") kan ikke symboliseres i denne build (Debian stripper
    # binærerne), så vi mapper dem til den bibliotek de ligger i. Det giver
    # stadig "hvor" tiden går (libxul, libc, libEGL, gralloc …).
    libs = []
    for lib in (profile.get("libs") or []):
        try:
            start = int(lib.get("start") or lib.get("offset") or 0, 0) if isinstance(lib.get("start"), str) else int(lib.get("start") or 0)
            end = int(lib.get("end") or 0, 0) if isinstance(lib.get("end"), str) else int(lib.get("end") or 0)
        except (TypeError, ValueError):
            continue
        libs.append((start, end, lib.get("debugName") or lib.get("name") or "?"))
    libs.sort()

    rows = []
    for ptype, th in threads_of(profile):
        try:
            counts, total = leaf_counts(th, libs)
        except Exception as exc:  # enkelt-thread-fejl skal ikke stoppe analysen
            print("  (kunne ikke læse tråd %s: %s)" % (th.get("name"), exc))
            continue
        rows.append((ptype, th.get("name", "?"), total, counts))
    if not rows:
        print("ingen tråde fundet — er filen en Gecko-profil?"); return 1

    rows.sort(key=lambda r: -r[2])
    interval = meta.get("interval", 1.0) or 1.0
    for ptype, name, total, counts in rows:
        if args.thread and args.thread not in name:
            continue
        if total < 20 and not args.thread:
            continue
        print("\n== [%s] %s: %d samples (~%.0f ms CPU)"
              % (ptype, name, total, total * interval))
        for fn, n in counts.most_common(args.top):
            print("   %6d  %5.1f%%  %s" % (n, 100.0 * n / total, fn))
    return 0


if __name__ == "__main__":
    sys.exit(main())
