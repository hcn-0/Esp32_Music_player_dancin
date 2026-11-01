#!/usr/bin/env python3
"""Run the Dancin-only ESP32 OLED player using the project's shared player."""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SONGS = Path(__file__).resolve().parent / "songs"
sys.path.insert(0, str(ROOT))

import music  # noqa: E402


def main() -> int:
    # Keep this entry point locked to its bundled track unless --songs is given.
    if "--songs" not in sys.argv[1:]:
        sys.argv[1:1] = ["--songs", str(SONGS)]
    return music.main()


if __name__ == "__main__":
    raise SystemExit(main())
