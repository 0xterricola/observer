import unittest

from v2_chat import BasecampChatReader, UnavailableChatReader


class BasecampChatReaderTests(unittest.TestCase):
    def test_native_transport_required(self):
        with self.assertRaises(TypeError):
            BasecampChatReader()

        with self.assertRaises(ValueError):
            BasecampChatReader(rpc_call=None)

    def test_native_call_delegation(self):
        calls = []

        def rpc_call(method, params):
            calls.append((method, params))
            return ["ok"]

        reader = BasecampChatReader(rpc_call=rpc_call)

        self.assertEqual(
            reader._call("list_conversations", None),
            ["ok"],
        )
        self.assertEqual(
            calls,
            [("list_conversations", None)],
        )

    def test_missing_native_ipc_is_unavailable(self):
        reader = UnavailableChatReader()

        expected = {
            "available": False,
            "reason": "native_chat_unavailable",
        }

        self.assertEqual(reader.read_conversations(), expected)
        self.assertEqual(
            reader.read_messages("example"),
            expected,
        )

    def test_chat_not_started(self):
        def rpc_call(method, params):
            self.assertEqual(
                method,
                "get_address",
            )
            return ""

        reader = BasecampChatReader(
            rpc_call=rpc_call
        )

        self.assertEqual(
            reader.read_conversations(),
            {
                "available": False,
                "reason":
                    "chat_not_started",
            },
        )

    def test_conversations(self):
        def rpc_call(method, params):
            if method == "get_address":
                return "abc123"

            if method == "list_conversations":
                return [
                    {
                        "convo_id": "c1",
                        "kind": "direct",
                        "message_count": 2,
                        "last_activity_ms": 200,
                        "preview": "gm",
                        "history_only": False,
                    }
                ]

            raise AssertionError(method)

        reader = BasecampChatReader(
            rpc_call=rpc_call
        )

        self.assertEqual(
            reader.read_conversations(),
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

    def test_messages_since_oldest_first(self):
        def rpc_call(method, params):
            if method == "get_address":
                return "abc123"

            if method == "get_messages":
                self.assertEqual(
                    params,
                    {
                        "convo_id": "c1",
                    },
                )

                return [
                    {
                        "from_self": False,
                        "content": "new",
                        "timestamp_ms": 300,
                        "sender": "peer",
                    },
                    {
                        "from_self": True,
                        "content": "mine",
                        "timestamp_ms": 200,
                    },
                    {
                        "from_self": False,
                        "content": "old",
                        "timestamp_ms": 100,
                        "sender": "peer",
                    },
                ]

            raise AssertionError(method)

        reader = BasecampChatReader(
            rpc_call=rpc_call
        )

        self.assertEqual(
            reader.read_messages(
                "c1",
                since_ms=100,
            ),
            {
                "available": True,
                "convo_id": "c1",
                "messages": [
                    {
                        "content": "mine",
                        "from_self": True,
                        "timestamp_ms": 200,
                    },
                    {
                        "content": "new",
                        "from_self": False,
                        "timestamp_ms": 300,
                        "sender": "peer",
                    },
                ],
            },
        )


if __name__ == "__main__":
    unittest.main()
