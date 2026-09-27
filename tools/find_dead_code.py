#!/usr/bin/env python3
"""
Reports declared-but-unused functions, methods and private members.

Dead code in a project like this is not just untidy: every branch in the shape
fragment shader is evaluated for every pixel, and every unused virtual is a
decision somebody has to re-read later.  This catches the leftovers that survive
a refactor.

    python3 tools/find_dead_code.py
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CPP = os.path.join(ROOT, "app/src/main/cpp")
SRC_DIRS = [os.path.join(CPP, d) for d in ("core", "game", "gfx", "ui", "audio", "meta", "platform")]
HEADERS = [os.path.join(CPP, "generated", f) for f in os.listdir(os.path.join(CPP, "generated"))]


def read_all():
    """Every C++ source in the project, as one lowercase blob for counting."""
    chunks = []
    for root_dir in SRC_DIRS:
        for dirpath, _dirnames, filenames in os.walk(root_dir):
            for name in filenames:
                if name.endswith((".cpp", ".h")):
                    with open(os.path.join(dirpath, name)) as fh:
                        chunks.append(fh.read())
    for h in HEADERS:
        with open(h) as fh:
            chunks.append(fh.read())
    for extra in ("hosttest/tests.cpp", "hosttest/linktest.cpp"):
        with open(os.path.join(ROOT, extra)) as fh:
            chunks.append(fh.read())
    return "\n".join(chunks)


DEF_RE = re.compile(
    r"^\s*(?:static\s+)?(?:inline\s+)?(?:constexpr\s+)?"
    r"(?:[A-Za-z_][\w:<>,\s\*&]*?)\b([A-Za-z_]\w*)\s*\([^;{]*\)\s*(?:const\s*)?\{",
    re.M,
)


def main():
    blob = read_all()
    findings = []
    seen = set()

    for root_dir in SRC_DIRS:
        for dirpath, _dirnames, filenames in os.walk(root_dir):
            for name in sorted(filenames):
                if not name.endswith(".cpp"):
                    continue
                path = os.path.join(dirpath, name)
                with open(path) as fh:
                    src = fh.read()
                for m in DEF_RE.finditer(src):
                    fn = m.group(1)
                    if fn in ("if", "for", "while", "switch", "catch", "return", "sizeof", "else"):
                        continue
                    key = (os.path.relpath(path, ROOT), fn)
                    if key in seen:
                        continue
                    seen.add(key)
                    uses = len(re.findall(r"\b%s\b" % re.escape(fn), blob))
                    # One for the definition itself, plus a declaration in the header.
                    if uses <= 2:
                        line = src[: m.start()].count("\n") + 1
                        findings.append("%s:%d  %s  (%d mentions)" % (key[0], line, fn, uses))

    for f in sorted(findings):
        print("unused?  %s" % f)
    if not findings:
        print("no unused function definitions found")
    return 0


if __name__ == "__main__":
    sys.exit(main())
