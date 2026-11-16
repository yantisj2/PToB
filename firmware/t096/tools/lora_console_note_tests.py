#!/usr/bin/env python3
"""Actual optional-note producer; cached console boundaries, no hardware.

Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
This tests observer hygiene, not the cause or recovery of a physical USB outage.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

APP = Path(__file__).resolve().parents[1]
GUARD = " struct r2_io io;r2_console_snapshot(&io);\n if(!io.dtr)return;\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--deps", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    out = args.out.resolve()
    tiny = args.deps.resolve() / "modules/crypto/tinycrypt/lib"
    sources = [APP / "tests/lora_console_note_test.c",
               APP / "src/lora_passive_core.c", APP / "src/lora_receiver_watch.c",
               APP / "protocol/lora_passive.c", APP / "src/lora_rc8_policy.c",
               APP / "protocol/lora_rc8.c", APP / "protocol/group_auth.c",
               *[tiny / "source" / (name + ".c") for name in ("hmac", "sha256", "utils")]]
    files = [*sources, APP / "tests/lora_ram_fixture.h", APP / "src/bounded_json.c",
             APP / "src/lora_passive_service.c", APP / "src/lora_hw.c", Path(__file__),
             *sorted((APP / "include").glob("*.h")), *sorted((APP / "protocol").glob("*.h"))]

    def manifest():
        return {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in files}

    before = manifest()
    fixture = (APP / "tests/lora_ram_fixture.h").read_text()
    app = (APP / "include/app.h").read_text()
    shape = r"struct r2_io \{[^}]+\};"
    assert re.search(shape, fixture).group() == re.search(shape, app).group(), "host console layout drift"
    owner = re.sub(r"^#include[^\n]*\n", "", (APP / "src/lora_passive_service.c").read_text(), flags=re.M)
    assert owner.count(GUARD) == 1, "negative control must remove exactly the cached-reader guard"
    hw = (APP / "src/lora_hw.c").read_text()
    start = hw.index("static struct r2_lora_hw_rejection rejected;")
    admission = out / "admission.c"
    admission.write_text(hw[start:hw.index("\n#endif\nstatic int ", start)])
    flags = [os.environ.get("CC", "/opt/homebrew/opt/llvm/bin/clang"), "-std=c11", "-O1", "-g",
             "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-unused-parameter",
             "-Wno-default-const-init-var-unsafe", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
             "-DR2_LORA_PASSIVE_SLOW=1", "-DR2_LORA_FIXED500_EXPERIMENT=1",
             '-DR2_HW_ADMISSION_BODY="' + str(admission) + '"',
             *["-I" + str(path) for path in (APP / "include", APP / "protocol", APP / "tests", tiny / "include")]]
    results = []
    for name, body in (("current", owner), ("without-reader-guard", owner.replace(GUARD, "", 1))):
        generated = out / (name + ".c")
        generated.write_text(body)
        binary = out / name
        command = [*flags, '-DR2_PASSIVE_SERVICE_BODY="' + str(generated) + '"',
                   *map(str, sources), "-o", str(binary)]
        compiled = subprocess.run(command, capture_output=True, text=True)
        (out / (name + "-compile.log")).write_text(compiled.stdout + compiled.stderr)
        compiled.check_returncode()
        for role in ("TX", "RX"):
            run = subprocess.run([str(binary), "1" if role == "TX" else "0"], capture_output=True, text=True)
            (out / (name + "-" + role + ".log")).write_text(run.stdout + run.stderr)
            if name == "current":
                run.check_returncode()
                assert "PASS cached closed-reader skips LoRa snapshot/formatting" in run.stdout
            else:
                assert run.returncode == 86 and "snapshots=0 locks=60 formats=60 attempts=60" in run.stderr, run.stdout + run.stderr
            results.append(dict(case=name, role=role, returncode=run.returncode, command=command,
                                generated_sha256=hashlib.sha256(body.encode()).hexdigest()))
            print((run.stdout + run.stderr).strip())
    assert before == manifest(), "input changed during test; receipt refused"
    (out / "receipt.json").write_text(json.dumps(dict(
        status="PASS_PRODUCTION_LORA_NOTE_CACHED_READER_GUARD", hardware=False,
        results=results, source_hashes=before,
        limits=["Observer hygiene only; does not reproduce or fix the physical USB outage.",
                "Cached DTR may lag hardware. Final nonblocking note admission remains authoritative.",
                "Synthetic time/console/radio boundaries; no USB operations, reset, flash or physical acceptance."]
    ), indent=2) + "\n")


if __name__ == "__main__":
    main()
