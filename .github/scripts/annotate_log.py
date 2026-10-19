#!/usr/bin/env python3
"""Print a build log as GitHub Actions notice annotations (readable on the run page
without signing in): the error lines first, then the tail of the log.

  annotate_log.py --title "SDL2 UWP build" --file build.log [--tail 25] [--max-errors 15]
"""
import argparse
import re
import sys

ERR = re.compile(r"(\berror\b|fatal error|: error |error MSB|error C\d+|LNK\d+)", re.I)


def annotate(title, text, limit=1800):
    t = text.strip() or "(empty)"
    if len(t) > limit:
        t = t[:limit] + " ...(truncated)"
    t = t.replace("%", "%25").replace("\r", "").replace("\n", "%0A")
    print(f"::notice title={title}::{t}", flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--title", required=True)
    ap.add_argument("--file", required=True)
    ap.add_argument("--tail", type=int, default=25)
    ap.add_argument("--head", type=int, default=0, help="also annotate the first N lines")
    ap.add_argument("--max-errors", type=int, default=15)
    a = ap.parse_args()

    try:
        with open(a.file, errors="replace") as f:
            lines = [l.rstrip() for l in f]
    except OSError as e:
        annotate(a.title + ": log missing", str(e))
        return 0

    seen, errs = set(), []
    for l in lines:
        if ERR.search(l) and l not in seen:
            seen.add(l)
            errs.append(l.strip()[:300])
    annotate(f"{a.title}: {len(errs)} distinct error lines, {len(lines)} log lines", "\n".join(errs[:a.max_errors]) or "(no error lines)")
    annotate(f"{a.title}: log tail", "\n".join(l.strip()[:300] for l in lines[-a.tail:]))
    if a.head:
        annotate(f"{a.title}: log head", "\n".join(l.strip()[:300] for l in lines[:a.head]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
