"""Real bounded TX core; manually stepped UART IRQ/FIFO, no radio hardware."""

from pathlib import Path
import os, subprocess, tempfile

r = Path(__file__).resolve().parents[1]
from host_build import compiler_flags, rt_log_dir

with tempfile.TemporaryDirectory(prefix="mesh-tx-test-") as temporary:
    for depth in (2, 4):
        exe = Path(temporary) / f"tx-{depth}"
        subprocess.run(
            [
                *compiler_flags(),
                "-Wall",
                "-Wextra",
                "-Werror",
                f"-DCONFIG_MESHTASTIC_TX_QUEUE_DEPTH={depth}",
                "-DCONFIG_MESHTASTIC_TX_TIMEOUT_MS=2000",
                *[
                    f"-I{x}"
                    for x in [
                        rt_log_dir() / "tests/host",
                        r / "tests/host",
                        r / "include",
                    ]
                ],
                str(r / "src/tx.c"),
                str(r / "src/frame.c"),
                str(r / "tests/test_tx.c"),
                "-o",
                str(exe),
            ],
            check=True,
        )
        subprocess.run([str(exe)], check=True)
