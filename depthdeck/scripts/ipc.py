#!/usr/bin/env python3
"""Client pro socket IPC do Wayfire (protocolo: 4 bytes LE de tamanho + JSON).

Uso:
  ./ipc.py window-rules/list-views
  ./ipc.py window-rules/focus-view '{"id": 42}'
  ./ipc.py wayfire/set-config-options '{"depthdeck/enabled": true}'
  ./ipc.py window-rules/configure-view '{"id": 42, "geometry": {"x":0,"y":0,"width":640,"height":480}}'

O socket é $XDG_RUNTIME_DIR/wayfire-<display>-.socket (criado pelo plugin ipc).
"""
import glob
import json
import os
import socket
import struct
import sys


def find_socket() -> str:
    if env := os.environ.get("WAYFIRE_SOCKET"):
        return env
    candidates = sorted(glob.glob(
        os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "wayfire-*.socket")))
    if not candidates:
        sys.exit("nenhum socket wayfire-*.socket encontrado (plugin ipc ativo?)")
    return candidates[0]


def call(method: str, data) -> dict:
    payload = json.dumps({"method": method, "data": data}).encode()
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
        s.connect(find_socket())
        s.sendall(struct.pack("<I", len(payload)) + payload)
        (length,) = struct.unpack("<I", _read_exact(s, 4))
        return json.loads(_read_exact(s, length))


def _read_exact(s: socket.socket, n: int) -> bytes:
    buf = b""
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            sys.exit("conexão fechada pelo compositor")
        buf += chunk
    return buf


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    method = sys.argv[1]
    data = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    print(json.dumps(call(method, data), indent=2, ensure_ascii=False))
