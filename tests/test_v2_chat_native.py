import json
import socket
import struct
import threading
import unittest

from v2_chat import BasecampChatReader
from v2_chat_native import NativeChatTransport


def receive(sock):
    header = sock.recv(4, socket.MSG_WAITALL)
    if len(header) != 4:
        raise EOFError("missing request length")
    length = struct.unpack("!I", header)[0]

    data = bytearray()
    while len(data) < length:
        chunk = sock.recv(length - len(data))
        if not chunk:
            raise EOFError("truncated request")
        data.extend(chunk)

    return json.loads(data)


def reply(sock, result):
    data = json.dumps({
        "ok": True,
        "result": result,
    }).encode()
    sock.sendall(struct.pack("!I", len(data)) + data)


class NativeChatTransportTests(unittest.TestCase):
    def test_rejects_write_operations(self):
        client, server = socket.socketpair()
        try:
            transport = NativeChatTransport(client)

            with self.assertRaises(ValueError):
                transport.call(
                    "send_message",
                    {"convo_id": "abc", "content": "hello"},
                )
        finally:
            client.close()
            server.close()


    def test_oversized_native_response(self):
        client, peer = socket.socketpair()
        try:
            transport = NativeChatTransport(client, timeout=1)
            peer.sendall(struct.pack("!I", 8 * 1024 * 1024 + 1))

            with self.assertRaises(ValueError):
                transport.call("get_address")

            self.assertTrue(transport.broken)
        finally:
            client.close()
            peer.close()

    def test_disconnected_native_peer(self):
        client, peer = socket.socketpair()
        try:
            transport = NativeChatTransport(client, timeout=1)
            peer.close()

            with self.assertRaises((OSError, EOFError)):
                transport.call("get_address")

            self.assertTrue(transport.broken)
        finally:
            client.close()

    def test_unresponsive_native_peer(self):
        client, peer = socket.socketpair()
        try:
            transport = NativeChatTransport(client, timeout=0.2)

            with self.assertRaises(TimeoutError):
                transport.call("get_address")

            self.assertTrue(transport.broken)
        finally:
            client.close()
            peer.close()

    def test_existing_chat_contract(self):
        client, server = socket.socketpair()
        server.settimeout(3)
        errors = []

        def serve():
            try:
                for expected, result in (
                    ("get_address", "test-address"),
                    ("list_conversations", [
                        {"convo_id": "abc", "message_count": 2}
                    ]),
                    ("get_address", "test-address"),
                    ("get_messages", [
                        {
                            "content": "new",
                            "from_self": False,
                            "timestamp_ms": 20,
                            "sender": "peer",
                        },
                        {
                            "content": "old",
                            "from_self": True,
                            "timestamp_ms": 10,
                        },
                    ]),
                ):
                    request = receive(server)
                    self.assertEqual(request["method"], expected)
                    reply(server, result)
            except Exception as exc:
                errors.append(exc)

        worker = threading.Thread(target=serve)
        worker.start()

        try:
            transport = NativeChatTransport(client, timeout=3)
            reader = BasecampChatReader(
                rpc_call=transport.call
            )

            conversations = reader.read_conversations()
            self.assertTrue(conversations["available"])
            self.assertEqual(
                conversations["conversations"][0]["convo_id"],
                "abc",
            )

            messages = reader.read_messages("abc", since_ms=10)
            self.assertEqual(
                [m["content"] for m in messages["messages"]],
                ["new"],
            )
        finally:
            client.close()
            server.close()
            worker.join(timeout=4)

        self.assertFalse(worker.is_alive())
        self.assertFalse(errors, str(errors))


if __name__ == "__main__":
    unittest.main()
