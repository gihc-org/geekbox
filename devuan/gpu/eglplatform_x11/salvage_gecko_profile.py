#!/usr/bin/env python3
"""Close an abruptly truncated Gecko profiler JSON document."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


CLOSE_FOR = {"{": "}", "[": "]"}


def open_json_stack(text: str) -> list[str]:
    stack: list[str] = []
    in_string = False
    escaped = False
    for char in text:
        if in_string:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            continue
        if char == '"':
            in_string = True
        elif char in CLOSE_FOR:
            stack.append(char)
        elif char in "}]":
            if not stack or CLOSE_FOR[stack[-1]] != char:
                raise ValueError(f"mismatched {char} after {len(stack)} levels")
            stack.pop()
    if in_string:
        raise ValueError("truncated inside a JSON string")
    return stack


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    text = args.input.read_text()
    try:
        json.loads(text)
    except json.JSONDecodeError:
        stack = open_json_stack(text)
        repaired = text + "".join(CLOSE_FOR[char] for char in reversed(stack))
        json.loads(repaired)
        args.output.write_text(repaired)
        print(
            f"repaired {args.input} -> {args.output}: "
            f"closed {len(stack)} JSON levels"
        )
    else:
        args.output.write_text(text)
        print(f"{args.input} was already valid; copied to {args.output}")


if __name__ == "__main__":
    main()
