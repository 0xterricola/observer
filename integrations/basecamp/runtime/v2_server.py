#!/usr/bin/env python3

import argparse
import json
from http.server import (
    BaseHTTPRequestHandler,
    ThreadingHTTPServer,
)
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

from v2_auth import (
    AuthError,
    NonceStore,
    authenticate_v2_request,
)
from v2_pairing import (
    PairingError,
    PairingStore,
)
from v2_node import (
    DEFAULT_UPSTREAM,
    LogosNodeReader,
)
import socket
from v2_chat_native import NativeChatTransport
from v2_chat import (
    BasecampChatReader,
    UnavailableChatReader,
)
from v2_status import (
    build_status_snapshot,
)
from v2_tls import (
    create_server_ssl_context,
    ensure_tls_identity,
)


MAX_JSON_BODY = 32 * 1024


def _json_body_length(headers):
    """
    Validate HTTP framing for the small JSON bodies accepted by v2.

    Transfer-Encoding is deliberately unsupported. Pairing requests
    must carry exactly one canonical decimal Content-Length.
    """
    if headers.get_all(
        "Transfer-Encoding"
    ):
        raise PairingError(
            "invalid_json",
            400,
        )

    lengths = (
        headers.get_all(
            "Content-Length"
        )
        or []
    )

    if len(lengths) != 1:
        raise PairingError(
            "invalid_json",
            400,
        )

    raw_length = lengths[0]

    if (
        not isinstance(
            raw_length,
            str,
        )
        or not raw_length
        or not raw_length.isascii()
        or not raw_length.isdecimal()
    ):
        raise PairingError(
            "invalid_json",
            400,
        )

    length = int(
        raw_length,
        10,
    )

    if (
        length <= 0
        or length > MAX_JSON_BODY
    ):
        raise PairingError(
            "invalid_json",
            400,
        )

    return length


class V2Handler(BaseHTTPRequestHandler):
    server_version = "LogosObserverV2/0"
    protocol_version = "HTTP/1.1"

    def log_message(
        self,
        format,
        *args,
    ):
        return

    def send_error(
        self,
        code,
        message=None,
        explain=None,
    ):
        if code == 501:
            self.send_json(
                404,
                {
                    "error":
                        "not_found",
                },
            )
            return

        super().send_error(
            code,
            message,
            explain,
        )

    def send_json(
        self,
        status,
        payload,
    ):
        body = json.dumps(
            payload,
            separators=(",", ":"),
        ).encode("utf-8")

        self.send_response(status)
        self.send_header(
            "Content-Type",
            "application/json",
        )
        self.send_header(
            "Content-Length",
            str(len(body)),
        )
        self.end_headers()
        self.wfile.write(body)

    def read_json(self):
        try:
            length = _json_body_length(
                self.headers
            )
        except PairingError:
            # Invalid framing can leave unread bytes on a persistent
            # connection. Do not attempt to parse another request on
            # that connection.
            self.close_connection = True
            raise

        body = self.rfile.read(length)

        try:
            payload = json.loads(
                body.decode("utf-8")
            )
        except (
            UnicodeDecodeError,
            json.JSONDecodeError,
        ):
            raise PairingError(
                "invalid_json",
                400,
            )

        if not isinstance(
            payload,
            dict,
        ):
            raise PairingError(
                "invalid_json",
                400,
            )

        return payload

    def authenticated_route_path(self):
        """
        Return only the routing path while preserving self.path
        unchanged for HMAC authentication.

        The signed request-target remains byte-for-byte the value
        received by BaseHTTPRequestHandler, including query ordering
        and percent encoding.
        """
        target = self.path

        if (
            not isinstance(target, str)
            or not target.startswith("/")
            or "#" in target
            or target.endswith("?")
        ):
            return None

        path, _, _query = target.partition("?")

        if not path:
            return None

        return path

    def authenticate(
        self,
        *,
        body=b"",
        required_scope=None,
    ):
        try:
            return authenticate_v2_request(
                pairing_store=
                    self.server.pairing_store,
                nonce_store=
                    self.server.nonce_store,
                method=self.command,
                request_target=self.path,
                headers=self.headers,
                body=body,
                required_scope=
                    required_scope,
                peer_address=
                    self.client_address[0],
            )
        except AuthError as exc:
            self.send_json(
                exc.status_code,
                {
                    "error":
                        exc.code,
                },
            )
            return None

    def send_read_section(
        self,
        *,
        required_scope,
        read,
    ):
        auth = self.authenticate(
            required_scope=
                required_scope,
        )

        if auth is None:
            return

        try:
            payload = read()
        except Exception:
            payload = None

        self.send_json(
            200,
            payload,
        )

    def do_GET(self):
        if self.path == "/health":
            self.send_json(
                200,
                {
                    "ok": True,
                    "service":
                        "logos-observer",
                    "protocol":
                        "logos-observer-v2",
                },
            )
            return

        route_path = (
            self.authenticated_route_path()
        )

        if route_path == "/v2/status":
            auth = self.authenticate()

            if auth is None:
                return

            snapshot = (
                build_status_snapshot(
                    device=auth["device"],
                    reader=
                        self.server.status_reader,
                )
            )

            self.send_json(
                200,
                snapshot,
            )
            return

        if route_path == "/v2/network":
            self.send_read_section(
                required_scope=
                    "network.status.read",
                read=(
                    self.server
                    .status_reader
                    .read_network
                ),
            )
            return

        if route_path == "/v2/mining":
            self.send_read_section(
                required_scope=
                    "mining.status.read",
                read=(
                    self.server
                    .status_reader
                    .read_mining
                ),
            )
            return

        if route_path == "/v2/rewards":
            self.send_read_section(
                required_scope=
                    "rewards.status.read",
                read=(
                    self.server
                    .status_reader
                    .read_rewards
                ),
            )
            return

        if route_path == "/v2/chat/conversations":
            self.send_read_section(
                required_scope=
                    "chat.read",
                read=(
                    self.server
                    .chat_reader
                    .read_conversations
                ),
            )
            return

        if route_path == "/v2/chat/messages":
            auth = self.authenticate(
                required_scope=
                    "chat.read",
            )

            if auth is None:
                return

            parsed = urlsplit(
                self.path
            )

            query = parse_qs(
                parsed.query,
                keep_blank_values=True,
            )

            if (
                set(query)
                - {"convo", "since_ms"}
            ):
                self.send_json(
                    400,
                    {
                        "error":
                            "invalid_query",
                    },
                )
                return

            convo_values = query.get(
                "convo",
                [],
            )

            since_values = query.get(
                "since_ms",
                [],
            )

            if (
                len(convo_values) != 1
                or not convo_values[0]
                or len(since_values) > 1
            ):
                self.send_json(
                    400,
                    {
                        "error":
                            "invalid_query",
                    },
                )
                return

            since_ms = None

            if since_values:
                since_text = (
                    since_values[0]
                )

                if (
                    not since_text
                    or not since_text.isascii()
                    or not since_text.isdigit()
                ):
                    self.send_json(
                        400,
                        {
                            "error":
                                "invalid_query",
                        },
                    )
                    return

                since_ms = int(
                    since_text
                )

            try:
                payload = (
                    self.server
                    .chat_reader
                    .read_messages(
                        convo_values[0],
                        since_ms=since_ms,
                    )
                )
            except Exception:
                payload = None

            self.send_json(
                200,
                payload,
            )
            return

        # /v2/blend remains reserved until its
        # v2 data contract is activated.
        self.send_json(
            404,
            {
                "error":
                    "not_found",
            },
        )

    def do_POST(self):
        if self.path != "/v2/pair":
            self.send_json(
                404,
                {
                    "error":
                        "not_found",
                },
            )
            return

        try:
            payload = self.read_json()

            secret = payload.get(
                "secret"
            )
            device_name = payload.get(
                "device_name"
            )
            scopes = payload.get(
                "scopes"
            )

            if not isinstance(
                secret,
                str,
            ):
                raise PairingError(
                    "invalid_bootstrap",
                    401,
                )

            result = (
                self.server
                .pairing_store
                .activate(
                    secret=secret,
                    device_name=
                        device_name,
                    scopes=scopes,
                    peer_address=
                        self.client_address[
                            0
                        ],
                )
            )

        except PairingError as exc:
            self.send_json(
                exc.status_code,
                {
                    "error":
                        exc.code,
                },
            )
            return

        self.send_json(
            200,
            result,
        )

    def do_DELETE(self):
        route_path = (
            self.authenticated_route_path()
        )

        if route_path != "/v2/device":
            self.send_json(
                404,
                {
                    "error":
                        "not_found",
                },
            )
            return

        auth = self.authenticate()

        if auth is None:
            return

        self.server.pairing_store.revoke_device(
            auth["device_id"]
        )

        self.send_json(
            200,
            {
                "revoked": True,
            },
        )


def main():
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--host",
        default="127.0.0.1",
    )

    parser.add_argument(
        "--port",
        type=int,
        default=8081,
    )

    parser.add_argument(
        "--advertise-host",
        default=None,
        help=(
            "address clients use to reach this "
            "Observer; defaults to --host"
        ),
    )

    parser.add_argument(
        "--state-dir",
        type=Path,
        default=Path(".v2-state"),
    )

    parser.add_argument(
        "--upstream",
        default=DEFAULT_UPSTREAM,
        help=(
            "local Logos node API; "
            "keep this bound to loopback"
        ),
    )

    parser.add_argument(
        "--chat-native-fd",
        type=int,
        default=None,
        help="Inherited private native Chat IPC descriptor",
    )

    args = parser.parse_args()

    advertise_host = (
        args.advertise_host
        or args.host
    )

    identity = ensure_tls_identity(
        args.state_dir,
        common_name="Logos Observer",
        dns_names=[
            "logos-observer.local",
        ],
        ip_addresses=[
            advertise_host,
        ],
    )

    pairing_store = PairingStore(
        args.state_dir
    )

    nonce_store = NonceStore(
        args.state_dir
    )

    status_reader = (
        LogosNodeReader(
            upstream=args.upstream,
        )
    )

    if args.chat_native_fd is not None:
        native_socket = socket.socket(
            fileno=args.chat_native_fd
        )
        chat_transport = NativeChatTransport(native_socket)
        chat_reader = BasecampChatReader(
            rpc_call=chat_transport.call
        )
    else:
        chat_reader = UnavailableChatReader()

    server = ThreadingHTTPServer(
        (
            args.host,
            args.port,
        ),
        V2Handler,
    )

    server.pairing_store = (
        pairing_store
    )

    server.nonce_store = (
        nonce_store
    )

    server.status_reader = (
        status_reader
    )

    server.chat_reader = (
        chat_reader
    )

    context = create_server_ssl_context(
        identity["cert_path"],
        identity["key_path"],
    )

    server.socket = context.wrap_socket(
        server.socket,
        server_side=True,
    )

    print(
        "Observer v2 HTTPS "
        f"listening on "
        f"{args.host}:{args.port}"
    )

    print(
        "Advertised endpoint: "
        f"{advertise_host}:{args.port}"
    )

    print(
        "SPKI pin: "
        f"{identity['pin']}"
    )

    print("TLS: 1.3 only")

    import signal

    def handle_sigterm(_signum, _frame):
        raise KeyboardInterrupt

    previous_handler = signal.getsignal(signal.SIGTERM)
    signal.signal(signal.SIGTERM, handle_sigterm)

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        signal.signal(signal.SIGTERM, previous_handler)
        server.server_close()


if __name__ == "__main__":
    import sys

    if len(sys.argv) > 1 and sys.argv[1] == "pair":
        sys.argv.pop(1)
        from v2_pair_cli import main as pairing_main
        pairing_main()
    else:
        main()
