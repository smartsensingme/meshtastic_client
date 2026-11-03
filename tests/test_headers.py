"""Compile public API as C11 and C++17, checking C linkage declarations."""

from pathlib import Path
import subprocess
import tempfile
from host_build import compiler_flags

root = Path(__file__).resolve().parents[1]
source = """
#include <meshtastic_client/client.h>
#include <meshtastic_client/frame.h>
#include <meshtastic_client/snapshot.h>
#include <meshtastic_client/diagnostics.h>
#ifdef __cplusplus
extern "C" int mc_local_extension_send(const uint8_t *, size_t);
extern "C" int mc_init(const struct device *, mc_callback);
extern "C" int mc_snapshot_read(struct mc_snapshot *);
#endif
int consume(struct mc_snapshot *snapshot) {
    return mc_snapshot_read(snapshot);
}
"""
with tempfile.TemporaryDirectory(prefix="mc-headers-") as temporary:
    for language, standard in (("c", "c11"), ("c++", "c++17")):
        compiler, *flags = compiler_flags()
        flags = [flag for flag in flags if not flag.startswith("-std=")]
        path = Path(temporary) / "headers.c"
        path.write_text(source)
        subprocess.run(
            [
                compiler,
                *flags,
                "-x",
                language,
                f"-std={standard}",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsyntax-only",
                "-DCONFIG_MESHTASTIC_NODE_CACHE_SIZE=32",
                f"-I{root / 'include'}",
                f"-I{root / 'tests/host'}",
                str(path),
            ],
            check=True,
        )
print("PASS: public headers C11/C++17 and C linkage")
