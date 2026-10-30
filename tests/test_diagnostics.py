"""Bounded radio diagnostic cache, reset and secret-exclusion checks."""

from pathlib import Path
import os, subprocess, tempfile

r = Path(__file__).resolve().parents[1]
from host_build import compiler_flags, rt_log_dir

with tempfile.TemporaryDirectory() as d:
    out = Path(d) / "diagnostics-test"
    subprocess.run(
        [
            *compiler_flags(),
            "-DCONFIG_MESHTASTIC_NODE_CACHE_SIZE=32",
            *[f"-I{r / x}" for x in ["include", "src", "generated", "nanopb"]],
            str(r / "src/diagnostics.c"),
            str(r / "tests/test_diagnostics.c"),
            "-o",
            str(out),
        ],
        check=True,
    )
    subprocess.run([str(out)], check=True)
print("Diagnostic cache checks passed")
