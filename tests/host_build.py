"""Host toolchain for standalone module tests (macOS/Linux, clang + sanitizers)."""

from pathlib import Path
import os
import shutil
import subprocess
import sys


def compiler_flags():
    """External test helpers call this; return compiler/flags, no device access."""
    args = [
        os.environ.get("CC", shutil.which("clang") or "clang"),
        "-std=c11",
        "-g",
        "-fsanitize=address,undefined",
    ]
    if sys.platform == "darwin":
        env = {
            **os.environ,
            "DEVELOPER_DIR": os.environ.get(
                "DEVELOPER_DIR", "/Applications/Xcode.app/Contents/Developer"
            ),
        }
        sdk = (
            os.environ.get("SDKROOT")
            or subprocess.check_output(
                ["xcrun", "--sdk", "macosx", "--show-sdk-path"], env=env, text=True
            ).strip()
        )
        args += ["-isysroot", sdk]
    return args


def rt_log_dir():
    """Locate explicit logger checkout or sibling repository, never application."""
    return Path(
        os.environ.get(
            "RT_LOG_MODULE_DIR", Path(__file__).resolve().parents[2] / "rt_log"
        )
    ).resolve()
