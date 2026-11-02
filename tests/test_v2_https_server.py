import http.client
import json
import ssl
import tempfile
import threading
import unittest
from http.server import ThreadingHTTPServer
from pathlib import Path
from urllib.parse import (
    parse_qs,
    urlsplit,
)

from v2_auth import NonceStore
from v2_crypto import (
    b64url_decode,
    b64url_encode,
    hmac_signature_v2,
)
from v2_pairing import PairingStore
from v2_server import V2Handler
from v2_tls import (
    create_server_ssl_context,
    ensure_tls_identity,
)


class ObserverV2HTTPSServerTests(
    unittest.TestCase
):
    def setUp(self):
        self.tempdir = (
            tempfile.TemporaryDirectory()
        )

        state_dir = Path(
            self.tempdir.name
        )

        identity = ensure_tls_identity(
            state_dir,
            common_name=(
                "Logos Observer Test"
            ),
            dns_names=[
                "localhost",
            ],
            ip_addresses=[
                "127.0.0.1",
            ],
        )

        self.pairing_store = (
            PairingStore(
                state_dir
            )
        )

        self.server = (
            ThreadingHTTPServer(
                (
                    "127.0.0.1",
                    0,
                ),
                V2Handler,
            )
        )

        self.server.pairing_store = (
            self.pairing_store
        )

        self.server.nonce_store = (
            NonceStore(
                state_dir
            )
        )

        class TestReader:
            def read_node(self):
                return {
                    "reachable": True,
                    "phase": "Following",
                    "height": 12345,
                    "tip": "abcdef",
                }

            def read_network(self):
                return {
                    "peers": 7,
                }

            def read_mining(self):
                return {
                    "is_mining": True,
                    "rewards_enabled": True,
                    "auto_claim": True,
                }

            def read_rewards(self):
                return {
                    "claimable_tickets": 0,
                    "slots_until_expiry": None,
                    "vouchers": 0,
                    "total_claimable": "0",
                }

            def read_blend(self):
                return None

        self.server.status_reader = (
            TestReader()
        )

        class TestChatReader:
            def read_conversations(self):
                return {
                    "available": True,
                    "conversations": [
                        {
                            "convo_id": "c1",
                            "kind": "direct",
                            "message_count": 2,
                            "last_activity_ms": 200,
                            "preview": "gm",
                            "history_only": False,
                        }
                    ],
                }

            def read_messages(
                self,
                convo_id,
                *,
                since_ms=None,
            ):
                messages = [
                    {
                        "content": "first",
                        "from_self": False,
                        "timestamp_ms": 100,
                        "sender": "peer",
                    },
                    {
                        "content": "second",
                        "from_self": True,
                        "timestamp_ms": 200,
                    },
                ]

                if since_ms is not None:
                    messages = [
                        message
                        for message in messages
                        if message["timestamp_ms"]
                        > since_ms
                    ]

                return {
                    "available": True,
                    "convo_id": convo_id,
                    "messages": messages,
                }

        self.server.chat_reader = (
            TestChatReader()
        )

        server_context = (
            create_server_ssl_context(
                identity[
                    "cert_path"
                ],
                identity[
                    "key_path"
                ],
            )
        )

        self.server.socket = (
            server_context.wrap_socket(
                self.server.socket,
                server_side=True,
            )
        )

        self.port = (
            self.server
            .server_address[1]
        )

        self.thread = (
            threading.Thread(
                target=(
                    self.server
                    .serve_forever
                ),
                daemon=True,
            )
        )

        self.thread.start()

        self.client_context = (
            ssl.create_default_context(
                cafile=str(
                    identity[
                        "cert_path"
                    ]
                )
            )
        )

        self.client_context.minimum_version = (
            ssl.TLSVersion.TLSv1_3
        )

        self.client_context.maximum_version = (
            ssl.TLSVersion.TLSv1_3
        )

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(
            timeout=2
        )
        self.tempdir.cleanup()

    def request(
        self,
        method,
        path,
        payload=None,
        headers=None,
    ):
        connection = (
            http.client
            .HTTPSConnection(
                "127.0.0.1",
                self.port,
                context=
                    self.client_context,
                timeout=2,
            )
        )

        connection.connect()

        tls_version = (
            connection.sock.version()
        )

        body = None
        headers = dict(
            headers or {}
        )

        if payload is not None:
            body = json.dumps(
                payload
            ).encode("utf-8")

            headers[
                "Content-Type"
            ] = "application/json"

            headers[
                "Content-Length"
            ] = str(len(body))

        connection.request(
            method,
            path,
            body=body,
            headers=headers,
        )

        response = (
            connection.getresponse()
        )

        response_body = (
            response
            .read()
            .decode("utf-8")
        )

        status = response.status

        connection.close()

        return (
            status,
            response_body,
            tls_version,
        )

    def test_health_over_tls13(
        self,
    ):
        status, body, tls = (
            self.request(
                "GET",
                "/health",
            )
        )

        self.assertEqual(
            status,
            200,
        )

        self.assertEqual(
            tls,
            "TLSv1.3",
        )

        self.assertIn(
            '"protocol":"logos-observer-v2"',
            body,
        )

    def test_unsupported_method_is_json_404(
        self,
    ):
        status, body, tls = self.request(
            "PUT",
            "/v2/status",
        )

        self.assertEqual(
            tls,
            "TLSv1.3",
        )

        self.assertEqual(
            status,
            404,
        )

        self.assertEqual(
            json.loads(body),
            {
                "error":
                    "not_found",
            },
        )


    def test_unknown_route_is_404(
        self,
    ):
        status, body, tls = (
            self.request(
                "GET",
                "/not-a-route",
            )
        )

        self.assertEqual(
            tls,
            "TLSv1.3",
        )

        self.assertEqual(
            status,
            404,
        )

        self.assertEqual(
            body,
            '{"error":"not_found"}',
        )

    def pair_device(
        self,
        scopes,
    ):
        offer = (
            self.pairing_store
            .create_offer(
                host=(
                    "127.0.0.1:"
                    f"{self.port}"
                ),
                pin="sha256/test",
                scopes=scopes,
            )
        )

        secret = parse_qs(
            urlsplit(
                offer["qr"]
            ).query
        )["s"][0]

        status, body, _ = (
            self.request(
                "POST",
                "/v2/pair",
                {
                    "secret":
                        secret,
                    "device_name":
                        "Casberi Test",
                    "scopes":
                        list(scopes),
                },
            )
        )

        self.assertEqual(
            status,
            200,
        )

        return json.loads(body)

    def signed_headers(
        self,
        pairing,
        *,
        method,
        path,
        nonce_byte,
        timestamp,
    ):
        nonce = b64url_encode(
            bytes(
                [nonce_byte]
                * 16
            )
        )

        signature = (
            hmac_signature_v2(
                b64url_decode(
                    pairing[
                        "hmac_key"
                    ]
                ),
                method,
                path,
                str(timestamp),
                nonce,
                pairing[
                    "device_id"
                ],
                b"",
            )
        )

        return {
            "X-Observer-Device":
                pairing[
                    "device_id"
                ],
            "X-Observer-Timestamp":
                str(timestamp),
            "X-Observer-Nonce":
                nonce,
            "X-Observer-Signature":
                signature,
        }

    def test_status_query_uses_exact_request_target(
        self,
    ):
        import time

        pairing = self.pair_device(
            [
                "node.status.read",
                "network.status.read",
                "mining.status.read",
                "rewards.status.read",
            ]
        )

        target = (
            "/v2/status"
            "?detail=mining%2Frewards"
            "&limit=10"
        )

        headers = self.signed_headers(
            pairing,
            method="GET",
            path=target,
            nonce_byte=90,
            timestamp=int(time.time()),
        )

        status, body, tls = self.request(
            "GET",
            target,
            headers=headers,
        )

        self.assertEqual(
            tls,
            "TLSv1.3",
        )

        self.assertEqual(
            status,
            200,
        )

        payload = json.loads(body)

        self.assertEqual(
            payload["v"],
            2,
        )

        self.assertIn(
            "node",
            payload,
        )

    def test_query_reordering_breaks_signature(
        self,
    ):
        import time

        pairing = self.pair_device(
            [
                "node.status.read",
            ]
        )

        signed_target = (
            "/v2/status"
            "?detail=node"
            "&limit=10"
        )

        sent_target = (
            "/v2/status"
            "?limit=10"
            "&detail=node"
        )

        headers = self.signed_headers(
            pairing,
            method="GET",
            path=signed_target,
            nonce_byte=91,
            timestamp=int(time.time()),
        )

        status, body, _ = self.request(
            "GET",
            sent_target,
            headers=headers,
        )

        self.assertEqual(
            status,
            401,
        )

        self.assertEqual(
            json.loads(body),
            {
                "error":
                    "bad_signature",
            },
        )

    def test_query_percent_encoding_breaks_signature(
        self,
    ):
        import time

        pairing = self.pair_device(
            [
                "node.status.read",
            ]
        )

        signed_target = (
            "/v2/status"
            "?detail=mining%2Frewards"
        )

        sent_target = (
            "/v2/status"
            "?detail=mining%2frewards"
        )

        headers = self.signed_headers(
            pairing,
            method="GET",
            path=signed_target,
            nonce_byte=92,
            timestamp=int(time.time()),
        )

        status, body, _ = self.request(
            "GET",
            sent_target,
            headers=headers,
        )

        self.assertEqual(
            status,
            401,
        )

        self.assertEqual(
            json.loads(body),
            {
                "error":
                    "bad_signature",
            },
        )

    def test_malformed_request_targets_are_not_routed(
        self,
    ):
        targets = (
            "/v2/status?",
            "/v2/status#fragment",
            (
                "https://observer.invalid"
                "/v2/status"
            ),
        )

        for target in targets:
            with self.subTest(
                target=target,
            ):
                status, body, _ = (
                    self.request(
                        "GET",
                        target,
                    )
                )

                self.assertEqual(
                    status,
                    404,
                )

                self.assertEqual(
                    json.loads(body),
                    {
                        "error":
                            "not_found",
                    },
                )


    def test_authenticated_status(
        self,
    ):
        import time

        pairing = self.pair_device(
            [
                "node.status.read",
                "network.status.read",
            ]
        )

        now = int(time.time())

        headers = self.signed_headers(
            pairing,
            method="GET",
            path="/v2/status",
            nonce_byte=1,
            timestamp=now,
        )

        status, body, tls = (
            self.request(
                "GET",
                "/v2/status",
                headers=headers,
            )
        )

        self.assertEqual(
            tls,
            "TLSv1.3",
        )

        self.assertEqual(
            status,
            200,
        )

        payload = json.loads(body)

        self.assertEqual(
            payload["v"],
            2,
        )

        self.assertEqual(
            payload["node"][
                "height"
            ],
            12345,
        )

        self.assertEqual(
            payload["network"][
                "peers"
            ],
            7,
        )

        self.assertNotIn(
            "mining",
            payload,
        )

        self.assertNotIn(
            "rewards",
            payload,
        )

    def test_status_replay_rejected(
        self,
    ):
        import time

        pairing = self.pair_device(
            [
                "node.status.read",
            ]
        )

        now = int(time.time())

        headers = self.signed_headers(
            pairing,
            method="GET",
            path="/v2/status",
            nonce_byte=2,
            timestamp=now,
        )

        first, _, _ = self.request(
            "GET",
            "/v2/status",
            headers=headers,
        )

        second, body, _ = self.request(
            "GET",
            "/v2/status",
            headers=headers,
        )

        self.assertEqual(
            first,
            200,
        )

        self.assertEqual(
            second,
            401,
        )

        self.assertEqual(
            json.loads(body),
            {
                "error": "replay",
            },
        )

    def test_self_revoke(
        self,
    ):
        import time

        pairing = self.pair_device(
            [
                "node.status.read",
            ]
        )

        now = int(time.time())

        revoke_headers = (
            self.signed_headers(
                pairing,
                method="DELETE",
                path="/v2/device",
                nonce_byte=3,
                timestamp=now,
            )
        )

        status, body, _ = (
            self.request(
                "DELETE",
                "/v2/device",
                headers=
                    revoke_headers,
            )
        )

        self.assertEqual(
            status,
            200,
        )

        self.assertEqual(
            json.loads(body),
            {
                "revoked": True,
            },
        )

        status_headers = (
            self.signed_headers(
                pairing,
                method="GET",
                path="/v2/status",
                nonce_byte=4,
                timestamp=now,
            )
        )

        status, body, _ = (
            self.request(
                "GET",
                "/v2/status",
                headers=
                    status_headers,
            )
        )

        self.assertEqual(
            status,
            401,
        )

        self.assertEqual(
            json.loads(body),
            {
                "error": "revoked",
            },
        )



    def test_native_chat_ipc_authorization_boundary(self):
        import socket
        import struct
        import time
        from v2_chat import BasecampChatReader
        from v2_chat_native import NativeChatTransport

        client, peer = socket.socketpair()
        received = []
        errors = []

        responses = [
            ("get_address", "test-address"),
            ("list_conversations", [
                {"convo_id": "c1", "message_count": 2}
            ]),
            ("get_address", "test-address"),
            ("get_messages", [
                {
                    "content": "first",
                    "from_self": False,
                    "timestamp_ms": 100,
                    "sender": "peer",
                },
                {
                    "content": "second",
                    "from_self": True,
                    "timestamp_ms": 200,
                },
            ]),
        ]

        def read_exact(count):
            data = bytearray()
            while len(data) < count:
                chunk = peer.recv(count - len(data))
                if not chunk:
                    raise EOFError("native peer disconnected")
                data.extend(chunk)
            return bytes(data)

        def serve_native():
            try:
                peer.settimeout(5)
                for expected, result in responses:
                    length = struct.unpack("!I", read_exact(4))[0]
                    if not 0 < length <= 8 * 1024 * 1024:
                        raise ValueError("invalid frame length")

                    request = json.loads(read_exact(length))
                    received.append(request)

                    if request.get("method") != expected:
                        raise AssertionError(
                            f"Expected {expected}, got {request}"
                        )

                    payload = json.dumps({
                        "ok": True,
                        "result": result,
                    }).encode("utf-8")

                    peer.sendall(
                        struct.pack("!I", len(payload)) + payload
                    )
            except Exception as exc:
                errors.append(repr(exc))
            finally:
                peer.close()

        worker = threading.Thread(target=serve_native, daemon=True)
        worker.start()

        previous_reader = self.server.chat_reader
        self.server.chat_reader = BasecampChatReader(
            rpc_call=NativeChatTransport(client, timeout=4).call
        )

        try:
            denied = self.pair_device(["node.status.read"])
            path = "/v2/chat/conversations"

            headers = self.signed_headers(
                denied,
                method="GET",
                path=path,
                nonce_byte=90,
                timestamp=int(time.time()),
            )

            status, body, _ = self.request(
                "GET", path, headers=headers
            )

            self.assertEqual(status, 403)
            self.assertEqual(json.loads(body), {"error": "scope"})
            self.assertEqual(received, [])

            allowed = self.pair_device(["chat.read"])

            headers = self.signed_headers(
                allowed,
                method="GET",
                path=path,
                nonce_byte=91,
                timestamp=int(time.time()),
            )

            status, body, _ = self.request(
                "GET", path, headers=headers
            )

            self.assertEqual(status, 200)
            self.assertEqual(
                json.loads(body)["conversations"],
                [{"convo_id": "c1", "message_count": 2}],
            )

            path = "/v2/chat/messages?convo=c1&since_ms=100"

            headers = self.signed_headers(
                allowed,
                method="GET",
                path=path,
                nonce_byte=92,
                timestamp=int(time.time()),
            )

            status, body, _ = self.request(
                "GET", path, headers=headers
            )

            self.assertEqual(status, 200)
            self.assertEqual(
                [m["content"] for m in json.loads(body)["messages"]],
                ["second"],
            )

            self.assertEqual(
                [request["method"] for request in received],
                [
                    "get_address",
                    "list_conversations",
                    "get_address",
                    "get_messages",
                ],
            )

        finally:
            self.server.chat_reader = previous_reader
            try:
                client.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            client.close()
            worker.join(timeout=5)

        self.assertFalse(worker.is_alive())
        self.assertEqual(errors, [])

    def test_chat_conversations_route(
        self,
    ):
        import time

        pairing = self.pair_device(
            [
                "chat.read",
            ]
        )

        path = "/v2/chat/conversations"
        now = int(time.time())

        headers = self.signed_headers(
            pairing,
            method="GET",
            path=path,
            nonce_byte=30,
            timestamp=now,
        )

        status, body, tls = (
            self.request(
                "GET",
                path,
                headers=headers,
            )
        )

        self.assertEqual(
            tls,
            "TLSv1.3",
        )

        self.assertEqual(
            status,
            200,
        )

        self.assertEqual(
            json.loads(body),
            {
                "available": True,
                "conversations": [
                    {
                        "convo_id": "c1",
                        "kind": "direct",
                        "message_count": 2,
                        "last_activity_ms": 200,
                        "preview": "gm",
                        "history_only": False,
                    }
                ],
            },
        )

    def test_chat_messages_route(
        self,
    ):
        import time

        pairing = self.pair_device(
            [
                "chat.read",
            ]
        )

        now = int(time.time())

        cases = [
            (
                "/v2/chat/messages?convo=c1",
                31,
                [
                    {
                        "content": "first",
                        "from_self": False,
                        "timestamp_ms": 100,
                        "sender": "peer",
                    },
                    {
                        "content": "second",
                        "from_self": True,
                        "timestamp_ms": 200,
                    },
                ],
            ),
            (
                (
                    "/v2/chat/messages?"
                    "convo=c1&since_ms=100"
                ),
                32,
                [
                    {
                        "content": "second",
                        "from_self": True,
                        "timestamp_ms": 200,
                    },
                ],
            ),
        ]

        for path, nonce_byte, messages in cases:
            headers = self.signed_headers(
                pairing,
                method="GET",
                path=path,
                nonce_byte=nonce_byte,
                timestamp=now,
            )

            status, body, tls = (
                self.request(
                    "GET",
                    path,
                    headers=headers,
                )
            )

            self.assertEqual(
                tls,
                "TLSv1.3",
            )

            self.assertEqual(
                status,
                200,
            )

            self.assertEqual(
                json.loads(body),
                {
                    "available": True,
                    "convo_id": "c1",
                    "messages": messages,
                },
            )

    def test_chat_scope_denied(
        self,
    ):
        import time

        pairing = self.pair_device(
            [
                "node.status.read",
            ]
        )

        path = "/v2/chat/conversations"

        headers = self.signed_headers(
            pairing,
            method="GET",
            path=path,
            nonce_byte=33,
            timestamp=int(time.time()),
        )

        status, body, _ = (
            self.request(
                "GET",
                path,
                headers=headers,
            )
        )

        self.assertEqual(
            status,
            403,
        )

        self.assertEqual(
            json.loads(body),
            {
                "error": "scope",
            },
        )

    def test_chat_messages_reject_invalid_query(
        self,
    ):
        import time

        pairing = self.pair_device(
            [
                "chat.read",
            ]
        )

        now = int(time.time())

        paths = [
            "/v2/chat/messages",
            "/v2/chat/messages?convo=",
            "/v2/chat/messages?convo=c1&since_ms=nope",
            "/v2/chat/messages?convo=c1&since_ms=-1",
            "/v2/chat/messages?convo=c1&since_ms=%2B1",
            "/v2/chat/messages?convo=c1&since_ms=1.5",
            "/v2/chat/messages?convo=c1&extra=x",
            "/v2/chat/messages?convo=c1&convo=c2",
            (
                "/v2/chat/messages?"
                "convo=c1&since_ms=1&since_ms=2"
            ),
        ]

        for index, path in enumerate(paths):
            headers = self.signed_headers(
                pairing,
                method="GET",
                path=path,
                nonce_byte=40 + index,
                timestamp=now,
            )

            status, body, _ = (
                self.request(
                    "GET",
                    path,
                    headers=headers,
                )
            )

            self.assertEqual(
                status,
                400,
            )

            self.assertEqual(
                json.loads(body),
                {
                    "error": "invalid_query",
                },
            )

    def test_dedicated_read_routes(
        self,
    ):
        import time

        pairing = self.pair_device(
            [
                "node.status.read",
                "network.status.read",
                "mining.status.read",
                "rewards.status.read",
            ]
        )

        now = int(time.time())

        cases = [
            (
                "/v2/network",
                10,
                {
                    "peers": 7,
                },
            ),
            (
                "/v2/mining",
                11,
                {
                    "is_mining": True,
                    "rewards_enabled":
                        True,
                    "auto_claim": True,
                },
            ),
            (
                "/v2/rewards",
                12,
                {
                    "claimable_tickets":
                        0,
                    "slots_until_expiry":
                        None,
                    "vouchers": 0,
                    "total_claimable":
                        "0",
                },
            ),
        ]

        for path, nonce_byte, expected in cases:
            headers = self.signed_headers(
                pairing,
                method="GET",
                path=path,
                nonce_byte=nonce_byte,
                timestamp=now,
            )

            status, body, tls = (
                self.request(
                    "GET",
                    path,
                    headers=headers,
                )
            )

            self.assertEqual(
                tls,
                "TLSv1.3",
            )

            self.assertEqual(
                status,
                200,
            )

            self.assertEqual(
                json.loads(body),
                expected,
            )

    def test_dedicated_route_scope_denied(
        self,
    ):
        import time

        pairing = self.pair_device(
            [
                "node.status.read",
            ]
        )

        now = int(time.time())

        headers = self.signed_headers(
            pairing,
            method="GET",
            path="/v2/network",
            nonce_byte=20,
            timestamp=now,
        )

        status, body, _ = (
            self.request(
                "GET",
                "/v2/network",
                headers=headers,
            )
        )

        self.assertEqual(
            status,
            403,
        )

        self.assertEqual(
            json.loads(body),
            {
                "error": "scope",
            },
        )

    def test_blend_route_remains_reserved(
        self,
    ):
        import time

        pairing = self.pair_device(
            [
                "node.status.read",
            ]
        )

        now = int(time.time())

        headers = self.signed_headers(
            pairing,
            method="GET",
            path="/v2/blend",
            nonce_byte=21,
            timestamp=now,
        )

        status, body, _ = (
            self.request(
                "GET",
                "/v2/blend",
                headers=headers,
            )
        )

        self.assertEqual(
            status,
            404,
        )

        self.assertEqual(
            json.loads(body),
            {
                "error": "not_found",
            },
        )


    def test_pair_activation_over_tls(
        self,
    ):
        offer = (
            self.pairing_store
            .create_offer(
                host=(
                    "127.0.0.1:"
                    f"{self.port}"
                ),
                pin="sha256/test",
            )
        )

        secret = parse_qs(
            urlsplit(
                offer["qr"]
            ).query
        )["s"][0]

        status, body, tls = (
            self.request(
                "POST",
                "/v2/pair",
                {
                    "secret":
                        secret,
                    "device_name":
                        "Casberi Test",
                    "scopes": [
                        "node.status.read",
                        "network.status.read",
                        "mining.status.read",
                        "rewards.status.read",
                    ],
                },
            )
        )

        self.assertEqual(
            tls,
            "TLSv1.3",
        )

        self.assertEqual(
            status,
            200,
        )

        payload = json.loads(body)

        self.assertTrue(
            payload[
                "device_id"
            ].startswith("d_")
        )

        self.assertEqual(
            len(
                payload[
                    "hmac_key"
                ]
            ),
            43,
        )

        self.assertEqual(
            payload[
                "granted_scopes"
            ],
            [
                "node.status.read",
                "network.status.read",
                "mining.status.read",
                "rewards.status.read",
            ],
        )


if __name__ == "__main__":
    unittest.main()
