"""Private, length-prefixed IPC transport for native Logos Chat reads."""

import json
import socket
import struct
import threading

MAX_FRAME = 8 * 1024 * 1024
READ_METHODS = frozenset({
    "get_address",
    "list_conversations",
    "get_messages",
})


class NativeChatTransport:
    def __init__(self, sock, *, timeout=4.0):
        if sock.family != socket.AF_UNIX:
            raise ValueError("native Chat requires a Unix socket")

        self.sock = sock
        self.timeout = timeout
        self.lock = threading.Lock()
        self.broken = False
        self.sock.set_inheritable(False)

    def _read_exact(self, count):
        parts = bytearray()

        while len(parts) < count:
            chunk = self.sock.recv(count - len(parts))
            if not chunk:
                raise EOFError("native Chat connection closed")
            parts.extend(chunk)

        return bytes(parts)

    def call(self, method, params=None):
        if method not in READ_METHODS:
            raise ValueError("native Chat method not permitted")

        if method == "get_messages":
            if (
                not isinstance(params, dict)
                or set(params) != {"convo_id"}
                or not isinstance(params["convo_id"], str)
                or not params["convo_id"]
            ):
                raise ValueError("invalid message request")
        elif params is not None:
            raise ValueError("unexpected native Chat parameters")

        request = {"method": method}
        if params is not None:
            request["params"] = params

        payload = json.dumps(
            request, separators=(",", ":")
        ).encode("utf-8")

        if not 0 < len(payload) <= MAX_FRAME:
            raise ValueError("native Chat request too large")

        with self.lock:
            if self.broken:
                raise ConnectionError("native Chat connection unavailable")

            try:
                self.sock.settimeout(self.timeout)
                self.sock.sendall(
                    struct.pack("!I", len(payload)) + payload
                )

                length = struct.unpack(
                    "!I", self._read_exact(4)
                )[0]

                if not 0 < length <= MAX_FRAME:
                    raise ValueError("invalid native Chat frame")

                response = json.loads(
                    self._read_exact(length).decode("utf-8")
                )

                if (
                    not isinstance(response, dict)
                    or type(response.get("ok")) is not bool
                    or (
                        response["ok"]
                        and "result" not in response
                    )
                ):
                    raise ValueError("invalid native Chat response")

            except (OSError, EOFError, ValueError):
                self.broken = True
                self.sock.close()
                raise

            if not response["ok"]:
                raise RuntimeError("native Chat operation failed")

            return response["result"]
