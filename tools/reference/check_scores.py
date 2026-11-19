#!/usr/bin/env python3
"""Check a recording made with --scores against the scoring rules as we implement them.

  python check_scores.py F:\\VibRibbonVR\\research\\reference\\scores.jsonl

Every record carries the score state just before the game acted. For each hit and
each stray press, this works out what the rules in core/src/gameplay.cpp say the
state should be afterwards and compares it with the next record.
"""
import json
import sys


def is_pair(road):
    return 4 <= road <= 9


def first_of(road):
    return road if road < 4 else 2 if road >= 9 else 1 if road >= 7 else 0


def second_of(road):
    return road if road < 4 else road - 6 if road >= 9 else road - 5 if road >= 7 else road - 3


def main():
    rows = [json.loads(line) for line in open(sys.argv[1], encoding="utf-8") if line.strip()]
    rows = [r for r in rows if "points" in r]
    if not rows:
        sys.exit("No score state in this file. Record it with --scores.")
    checked = wrong = hits = presses = 0
    strays = []  # roads of the stray presses counted since the last hit or miss
    for now, after in zip(rows, rows[1:]):
        if after["type"] == "round_start" or now["type"] == "round_start":
            strays = []
            continue
        if now["strays"] == 0:
            strays = []
        problems = []
        if now["type"] == "hit":
            hits += 1
            road, times = now["road"], now["times"]
            if times != (2 if now["level"] < 3 else 1):
                problems.append("multiplier %d at level %d" % (times, now["level"]))
            base = 3 if is_pair(road) else 1
            bonus = now["stray_bonus"] if now["strays"] else 0
            want = now["points"] + (base + bonus + now["chain"]) * times
            if after["points"] != want:
                problems.append("points %d, expected %d" % (after["points"], want))
            # A miss between this hit and the next record clears the chain.
            if after["chain"] not in (min(now["chain"] + 1, 10), 0):
                problems.append("chain %d after %d" % (after["chain"], now["chain"]))
            strays = []
        elif now["type"] == "press":
            presses += 1
            road, part = now["road"], now["part_road"]
            if now["strays"] < 2:
                match = road in (first_of(part), second_of(part))
                result = 1 if (road in strays or match) else 4 if is_pair(part) else 2
                want_bonus = (now["stray_bonus"] if now["strays"] else 0) + result
                # A miss before the next record resets the count; then nothing can be checked.
                if after["strays"] == now["strays"] + 1 and after["stray_bonus"] != want_bonus:
                    problems.append("stray bonus %d, expected %d" % (after["stray_bonus"], want_bonus))
                strays.append(road)
            elif after["strays"] == 2 and after["stray_bonus"] != now["stray_bonus"]:
                problems.append("a third stray changed the bonus")
        else:
            if after["points"] != now["points"]:
                problems.append("points changed without a hit")
        if not 0 <= now["level"] <= 11 or not 0 <= now["combo"] < 18 or not 0 <= now["misses"] < 3 or not 0 <= now["chain"] <= 10:
            problems.append("state out of range: level %d combo %d misses %d chain %d" % (now["level"], now["combo"], now["misses"], now["chain"]))
        checked += 1
        if problems:
            wrong += 1
            print("record %d (%s): %s" % (rows.index(now) + 1, now["type"], "; ".join(problems)))
    print("%d records checked: %d hits, %d stray presses, %d disagree with our rules" % (checked, hits, presses, wrong))
    print("SCORING MATCHES" if wrong == 0 and hits > 0 else "DIFFERENCES FOUND" if wrong else "No hits in the recording")
    return 1 if wrong else 0


if __name__ == "__main__":
    sys.exit(main())
