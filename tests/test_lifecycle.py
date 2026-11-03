"""Standalone client lifecycle test; requires only client + rt_log, no applications."""

from pathlib import Path
import os, subprocess, tempfile

r = Path(__file__).resolve().parents[1]
from host_build import compiler_flags, rt_log_dir

log = rt_log_dir()
settings = {
    "RT_LOG": 1,
    "RT_LOG_DYNAMIC_LEVEL_CONTROL": 1,
    "RT_LOG_MAX_LEVEL": 5,
    "RT_LOG_DEFAULT_LEVEL": 4,
    "RT_LOG_TAG_SLOTS": 16,
    "RT_LOG_TAG_MAX_LEN": 31,
    "MESHTASTIC_TX_QUEUE_DEPTH": 4,
    "MESHTASTIC_TX_TIMEOUT_MS": 2000,
    "MESHTASTIC_NODE_CACHE_SIZE": 32,
    "MESHTASTIC_PACKET_TRACE": 0,
    "MESHTASTIC_LIVENESS_TIMEOUT_MS": 180000,
    "MESHTASTIC_HEARTBEAT_INTERVAL_MS": 60000,
    "MESHTASTIC_SYNC_TIMEOUT_MS": 20000,
    "MESHTASTIC_RETRY_MAX_MS": 300000,
    "MESHTASTIC_REFRESH_INTERVAL_MS": 0,
    "MESHTASTIC_WANT_ACK": 1,
    "MESHTASTIC_HOP_LIMIT": 1,
    "MESHTASTIC_LOCAL_EXTENSION": 0,
}

sources = [
    log / "src/log.c",
    r / "tests/test_lifecycle.c",
    r / "src/frame.c",
    r / "src/tx.c",
    r / "src/diagnostics.c",
    *sorted((r / "generated/meshtastic").glob("*.pb.c")),
    *[r / "nanopb" / n for n in ["pb_common.c", "pb_encode.c", "pb_decode.c"]],
]
import sys

if "--local-extension" in sys.argv:
    settings["MESHTASTIC_LOCAL_EXTENSION"] = 1
if "--tx-depth=2" in sys.argv:
    settings["MESHTASTIC_TX_QUEUE_DEPTH"] = 2
with tempfile.TemporaryDirectory(prefix="client-state-test-") as d:
    exe = Path(d) / "test"
    subprocess.run(
        [
            *compiler_flags(),
            "-Wall",
            "-Wextra",
            "-Werror",
            *[f"-DCONFIG_{k}={v}" for k, v in settings.items()],
            *[
                f"-I{x}"
                for x in [
                    log / "tests/host",
                    log / "include",
                    r / "tests/host",
                    r / "include",
                    r / "generated",
                    r / "nanopb",
                ]
            ],
            *map(str, sources),
            "-o",
            str(exe),
        ],
        check=True,
    )
    subprocess.run([str(exe)], check=True)
