#!/usr/bin/env python3
"""Prepare a modular ZMK keymap for the SVG drawing tool.

LiTom keeps each layer in a separate file as an ``&keymap { ... }`` overlay.
The drawing tool expects all layer nodes inside one ``keymap { ... }`` node,
so this script expands local includes and creates that drawing-only view.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path


LOCAL_INCLUDE = re.compile(r'^\s*#include\s+"([^"]+)"', re.MULTILINE)
KEYMAP_OVERLAY = re.compile(r"&keymap\s*\{")


def expand_local_includes(source: Path, stack: tuple[Path, ...] = ()) -> str:
    source = source.resolve()
    if source in stack:
        chain = " -> ".join(str(path) for path in (*stack, source))
        raise ValueError(f"Recursive include detected: {chain}")

    text = source.read_text(encoding="utf-8-sig")

    def replace_include(match: re.Match[str]) -> str:
        include = (source.parent / match.group(1)).resolve()
        if not include.is_file():
            raise FileNotFoundError(f"Include not found: {include}")
        return expand_local_includes(include, (*stack, source))

    return LOCAL_INCLUDE.sub(replace_include, text)


def find_block_body(text: str, opening_brace: int) -> tuple[str, int]:
    depth = 1
    cursor = opening_brace + 1

    while cursor < len(text) and depth:
        if text[cursor] == "{":
            depth += 1
        elif text[cursor] == "}":
            depth -= 1
        cursor += 1

    if depth:
        raise ValueError("Unclosed &keymap block")

    return text[opening_brace + 1 : cursor - 1], cursor


def collect_keymap_overlays(text: str) -> list[str]:
    bodies: list[str] = []
    cursor = 0

    while match := KEYMAP_OVERLAY.search(text, cursor):
        opening_brace = text.find("{", match.start())
        body, cursor = find_block_body(text, opening_brace)
        bodies.append(body.strip())

    if not bodies:
        raise ValueError("No &keymap overlay blocks were found")

    return bodies


def prepare_keymap(source: Path) -> str:
    expanded = expand_local_includes(source)
    layers = "\n\n".join(collect_keymap_overlays(expanded))
    return f"/ {{\n    keymap {{\n{layers}\n    }};\n}};\n\n{expanded}\n"


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Create a drawing-only keymap with modular layers merged."
    )
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(prepare_keymap(args.source), encoding="utf-8")


if __name__ == "__main__":
    main()
