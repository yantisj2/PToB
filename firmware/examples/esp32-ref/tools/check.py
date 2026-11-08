#!/usr/bin/env python3
"""Build and check ESP32-REF 00 on the host; never access hardware.

Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "components" / "ref_clock"
LESSON = ROOT / "00_clock_fundamentals"


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_manifest():
    """Only public collection files; no Git checkout or parent target needed."""
    return {
        str(path.relative_to(ROOT)): sha256(path)
        for path in sorted(ROOT.rglob("*"))
        if path.is_file() and "__pycache__" not in path.parts
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True,
                        help="fresh output directory outside the source collection")
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"),
                        help="C compiler command (default: CC or cc)")
    parser.add_argument("--sanitize", nargs="?", const="address,undefined",
                        choices=["address", "undefined", "address,undefined"],
                        help="enable sanitizers (default when supplied: address,undefined)")
    args = parser.parse_args()
    out = args.out.resolve()
    if out == ROOT or ROOT in out.parents:
        parser.error("put --out outside the source collection")
    if out.exists():
        parser.error("--out must be a fresh directory; previous evidence is preserved")
    compiler = shlex.split(args.cc)
    if not compiler:
        parser.error("--cc must name a compiler")

    os.umask(0o077)
    out.mkdir(parents=True)
    manifest = source_manifest()
    (out / "sources.json").write_text(json.dumps(manifest, indent=2) + "\n")
    commands = []
    receipt = {
        "schema": 1,
        "example": "ESP32-REF/00_clock_fundamentals",
        "evidence_kind": "DETERMINISTIC_HOST_EXECUTION",
        "status": "FAILED",
        "hardware_access": False,
        "physical_precision": "UNMEASURED",
        "sanitizers": args.sanitize,
        "python_version": sys.version,
        "commands": commands,
        "source_manifest_sha256": sha256(out / "sources.json"),
    }

    def run(argv, log_name):
        commands.append({"argv": [str(arg) for arg in argv], "log": log_name})
        try:
            result = subprocess.run(argv, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, check=False, timeout=60)
        except subprocess.TimeoutExpired as error:
            partial = error.stdout or b""
            if isinstance(partial, bytes):
                partial = partial.decode("utf-8", errors="replace")
            (out / log_name).write_text(partial + "\nCommand exceeded 60 seconds.\n")
            commands[-1]["timed_out"] = True
            raise RuntimeError("command timed out; see " + str(out / log_name)) from None
        (out / log_name).write_text(result.stdout)
        commands[-1]["returncode"] = result.returncode
        if result.returncode != 0:
            raise RuntimeError("command failed; see " + str(out / log_name))
        return result.stdout

    try:
        run(compiler + ["--version"], "compiler.log")
        flags = ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Wpedantic",
                 "-Wconversion", "-Wshadow", "-Werror"]
        if args.sanitize:
            flags += ["-fsanitize=" + args.sanitize, "-fno-sanitize-recover=all",
                      "-fno-omit-frame-pointer"]
        includes = ["-I", str(CORE), "-I", str(LESSON)]
        sources = [CORE / "ref_counter.c", CORE / "ref_clock.c", LESSON / "lesson.c",
                   LESSON / "main.c", ROOT / "tests" / "core_test.c"]
        for source in sources:
            run(compiler + flags + includes + ["-MMD", "-MF", str(out / (source.stem + ".d")),
                "-c", str(source), "-o", str(out / (source.stem + ".o"))],
                source.stem + "-compile.log")

        # -MMD lists non-system dependencies. Inspect what the compiler actually
        # included, so source isolation is checked beyond a text search.
        dependencies = set()
        for depfile in sorted(out.glob("*.d")):
            tokens = shlex.split(depfile.read_text().replace("\\\n", " "))
            for name in tokens[1:]:
                path = Path(name).resolve()
                if ROOT not in path.parents:
                    raise RuntimeError("dependency outside ESP32-REF: " + str(path))
                dependencies.add(str(path.relative_to(ROOT)))
        receipt["compiled_dependencies"] = sorted(dependencies)

        core_objects = [str(out / "ref_counter.o"), str(out / "ref_clock.o")]
        run(compiler + flags + core_objects + [str(out / "lesson.o"), str(out / "main.o"),
            "-o", str(out / "clock_fundamentals")], "lesson-link.log")
        run(compiler + flags + core_objects + [str(out / "core_test.o"),
            "-o", str(out / "core_test")], "tests-link.log")
        run([str(out / "clock_fundamentals")], "lesson.log")
        result = run([str(out / "core_test")], "tests.log")
        if source_manifest() != manifest:
            raise RuntimeError("source changed during the check; run again in fresh output")
        receipt["status"] = "HOST_CHECKS_PASSED"
        print(result, end="")
        print("PASS: compiled dependencies stay inside ESP32-REF")
    except (OSError, RuntimeError, ValueError) as error:
        receipt["error"] = str(error)
        print(str(error), file=sys.stderr)
    except KeyboardInterrupt:
        receipt["error"] = "Check interrupted by operator"
        print(receipt["error"], file=sys.stderr)
    finally:
        receipt["artifacts"] = {
            path.name: sha256(path) for path in sorted(out.iterdir()) if path.is_file()
        }
        (out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
        print("Receipt: " + str(out / "receipt.json"))
    return 0 if receipt["status"] == "HOST_CHECKS_PASSED" else 1


if __name__ == "__main__":
    sys.exit(main())
