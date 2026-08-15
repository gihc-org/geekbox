#!/usr/bin/env python3
"""
Analyser token-forbrug for en Kimi Code-session ud fra dens wire.jsonl.

Brug:
  ./analyze_kimi_usage.py                    # seneste ændrede session automatisk
  ./analyze_kimi_usage.py <sti>/wire.jsonl   # specifik session

Viser: forbrug pr. dag (requests + tokens) og kontekst-vækst over tid.
Se FORBRUG.md for fortolkning og strategier.
"""
import json, sys, datetime, collections, glob, os

def find_latest():
    files = glob.glob(os.path.expanduser("~/.kimi-code/sessions/*/*/agents/main/wire.jsonl"))
    if not files:
        sys.exit("ingen wire.jsonl fundet — angiv sti som argument")
    return max(files, key=os.path.getmtime)

W = sys.argv[1] if len(sys.argv) > 1 else find_latest()
print(f"analyserer: {W}\n")

recs = []
for line in open(W):
    try:
        e = json.loads(line)
        if e.get("type") == "usage.record":
            u = e["usage"]
            recs.append((e["time"] / 1000, u.get("inputOther", 0),
                         u.get("inputCacheRead", 0), u.get("inputCacheCreation", 0),
                         u.get("output", 0)))
    except Exception:
        pass

if not recs:
    sys.exit("ingen usage.record-hændelser fundet i filen")

DK = datetime.timezone(datetime.timedelta(hours=2))  # dansk tid (CEST sommer)
def dk(ts): return datetime.datetime.fromtimestamp(ts, DK)

days = collections.defaultdict(lambda: [0, 0, 0, 0, 0])
for ts, io, cr, cc, out in recs:
    v = days[dk(ts).strftime("%Y-%m-%d")]
    v[0] += io; v[1] += cr; v[2] += cc; v[3] += out; v[4] += 1

print(f"{'dato':<11}{'req':>6}{'uncached-in':>13}{'cache-read':>13}{'cache-ny':>10}{'output':>9}{'TOTAL':>12}")
tot = [0, 0, 0, 0, 0]
for d in sorted(days):
    v = days[d]
    print(f"{d:<11}{v[4]:>6}{v[0]:>13,}{v[1]:>13,}{v[2]:>10,}{v[3]:>9,}{sum(v[:4]):>12,}")
    for i in range(5): tot[i] += v[i]
print(f"\nTOTALT: {tot[4]} requests, {sum(tot[:4]):,} tokens")
print(f"  uncached input: {tot[0]:,}  |  cache-read: {tot[1]:,}  |  cache-ny: {tot[2]:,}  |  output: {tot[3]:,}")

print("\nkontekst-størrelse pr. request over tid:")
step = max(1, len(recs) // 15)
for i in range(0, len(recs), step):
    ts, io, cr, cc, out = recs[i]
    print(f"  {dk(ts).strftime('%d/%m %H:%M')}  req#{i+1:<4} kontekst ≈ {io+cr:>10,} tokens")
