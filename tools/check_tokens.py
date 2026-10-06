#!/usr/bin/env python3
"""Token contract check (review 2026-10-06, 06-R7).

DESIGN.md and include/flux/Theme.hpp each hold a full copy of the design
tokens (and nfui holds a third). This script diffs the two in-repo copies
name-by-name and value-by-value so a drift fails CI instead of relying on
the hand-maintained smoke assertions alone.

Usage: python tools/check_tokens.py  (from the repository root)
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

TOKEN_RE_HPP = re.compile(
    r"inline\s+constexpr\s+(?:int|float)\s+(\w+)\s*=\s*([0-9.]+)f?\s*;")
TOKEN_RE_MD = re.compile(r"^\|\s*`(\w+)`\s*\|\s*`([0-9.]+)`\s*\|")


def parse_theme_hpp(path: Path) -> dict[str, str]:
    tokens: dict[str, str] = {}
    inside = False
    for line in path.read_text(encoding="utf-8").splitlines():
        if "namespace tokens" in line:
            inside = True
        elif inside and line.strip().startswith("}"):
            break
        elif inside:
            m = TOKEN_RE_HPP.search(line)
            if m:
                tokens[m.group(1)] = m.group(2)
    return tokens


def parse_design_md(path: Path) -> dict[str, str]:
    tokens: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        m = TOKEN_RE_MD.match(line)
        if m:
            tokens[m.group(1)] = m.group(2)
    return tokens


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    hpp = parse_theme_hpp(root / "include" / "flux" / "Theme.hpp")
    md = parse_design_md(root / "DESIGN.md")

    failures = 0
    for name in sorted(hpp):
        if name not in md:
            print(f"[FAIL] {name} = {hpp[name]} in Theme.hpp but missing from DESIGN.md")
            failures += 1
        elif hpp[name] != md[name]:
            print(f"[FAIL] {name}: Theme.hpp = {hpp[name]} but DESIGN.md = {md[name]}")
            failures += 1
    for name in sorted(md):
        if name not in hpp:
            print(f"[FAIL] {name} = {md[name]} in DESIGN.md but missing from Theme.hpp")
            failures += 1

    if failures:
        print(f"token contract: {failures} mismatch(es)")
        return 1
    print(f"token contract: {len(hpp)} tokens consistent across DESIGN.md and Theme.hpp")
    return 0


if __name__ == "__main__":
    sys.exit(main())
