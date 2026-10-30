"""Run with a Python environment containing the official meshtastic package and clang."""

from pathlib import Path
import subprocess
import os
import sys
import shutil
import tempfile
from meshtastic.protobuf import mesh_pb2

root = Path(__file__).resolve().parents[1]


def arr(name, data):
    return f"const unsigned char {name}[]={{" + ",".join(str(x) for x in data) + "};\n"


def frame(message):
    data = message.SerializeToString()
    return b"\x94\xc3" + len(data).to_bytes(2, "big") + data


messages = [
    mesh_pb2.FromRadio(my_info=mesh_pb2.MyNodeInfo(my_node_num=0x3FD7D85C)),
    mesh_pb2.FromRadio(config_complete_id=123456),
    mesh_pb2.FromRadio(
        packet=mesh_pb2.MeshPacket(
            **{"from": 0x1234ABCD},
            to=0x3FD7D85C,
            channel=1,
            decoded=mesh_pb2.Data(portnum=256, payload=b"hello"),
        )
    ),
]
out = mesh_pb2.ToRadio(
    packet=mesh_pb2.MeshPacket(
        to=0x1234ABCD,
        channel=1,
        id=42,
        hop_limit=1,
        want_ack=True,
        priority=mesh_pb2.MeshPacket.RELIABLE,
        decoded=mesh_pb2.Data(portnum=256, payload=b"hello"),
    )
)
with tempfile.TemporaryDirectory(prefix="mc-wire-test-") as temp:
    tmp = Path(temp)
    (tmp / "fixtures.h").write_text(
        arr("inbound", b"".join(map(frame, messages)))
        + arr("expected_out", out.SerializeToString())
    )
    sources = [
        root / "src/frame.c",
        root / "tests/wire_test.c",
        *sorted((root / "generated/meshtastic").glob("*.pb.c")),
        *[root / "nanopb" / x for x in ("pb_common.c", "pb_encode.c", "pb_decode.c")],
    ]
    compiler = os.environ.get("CC", shutil.which("clang") or "clang")
    extra = []
    if sys.platform == "darwin":
        compiler = os.environ.get("CC", "/usr/bin/clang")
        sdk = os.environ.get("SDKROOT")
        if not sdk:
            sdk = subprocess.check_output(
                ["xcrun", "--sdk", "macosx", "--show-sdk-path"],
                env={
                    **os.environ,
                    "DEVELOPER_DIR": "/Applications/Xcode.app/Contents/Developer",
                },
                text=True,
            ).strip()
        extra = ["-isysroot", sdk]
    subprocess.run(
        [
            compiler,
            "-std=c11",
            *extra,
            "-g",
            "-fsanitize=address,undefined",
            "-I" + str(root / "include"),
            "-I" + str(root / "generated"),
            "-I" + str(root / "nanopb"),
            "-I" + temp,
            *map(str, sources),
            "-o",
            str(tmp / "wire_test"),
        ],
        check=True,
    )
    subprocess.run([str(tmp / "wire_test")], check=True)
