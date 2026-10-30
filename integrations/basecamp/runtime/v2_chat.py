#!/usr/bin/env python3

MAX_MESSAGES = 500


class BasecampChatReader:
    """Read-only Chat adapter using an injected native transport."""

    def __init__(self, *, rpc_call):
        if not callable(rpc_call):
            raise ValueError("Native Chat transport callback required")
        self._rpc_call = rpc_call

    def _call(self, method, params=None):
        return self._rpc_call(method, params)

    def _available(self):
        address = self._call(
            "get_address"
        )

        return (
            isinstance(address, str)
            and bool(address)
        )

    @staticmethod
    def _unavailable():
        return {
            "available": False,
            "reason": "chat_not_started",
        }

    def read_conversations(self):
        if not self._available():
            return self._unavailable()

        conversations = self._call(
            "list_conversations"
        )

        if not isinstance(
            conversations,
            list,
        ):
            raise ValueError(
                "conversation result is not an array"
            )

        result = []

        for conversation in conversations:
            if not isinstance(
                conversation,
                dict,
            ):
                raise ValueError(
                    "conversation is not an object"
                )

            convo_id = conversation.get(
                "convo_id"
            )

            if not isinstance(
                convo_id,
                str,
            ) or not convo_id:
                raise ValueError(
                    "conversation has invalid convo_id"
                )

            item = {
                "convo_id": convo_id,
            }

            for key in (
                "nickname",
                "message_count",
                "last_activity_ms",
                "kind",
                "name",
                "description",
                "preview",
                "history_only",
            ):
                if key in conversation:
                    item[key] = (
                        conversation[key]
                    )

            result.append(item)

        return {
            "available": True,
            "conversations": result,
        }

    def read_messages(
        self,
        convo_id,
        *,
        since_ms=None,
    ):
        if not self._available():
            return self._unavailable()

        if not isinstance(
            convo_id,
            str,
        ) or not convo_id:
            raise ValueError(
                "invalid convo_id"
            )

        messages = self._call(
            "get_messages",
            {
                "convo_id":
                    convo_id,
            },
        )

        if not isinstance(
            messages,
            list,
        ):
            raise ValueError(
                "message result is not an array"
            )

        result = []

        for message in messages:
            if not isinstance(
                message,
                dict,
            ):
                raise ValueError(
                    "message is not an object"
                )

            content = message.get(
                "content"
            )
            timestamp_ms = message.get(
                "timestamp_ms"
            )
            from_self = message.get(
                "from_self"
            )

            if not isinstance(
                content,
                str,
            ):
                raise ValueError(
                    "message content is invalid"
                )

            if (
                isinstance(timestamp_ms, bool)
                or not isinstance(
                    timestamp_ms,
                    int,
                )
            ):
                raise ValueError(
                    "message timestamp is invalid"
                )

            if not isinstance(
                from_self,
                bool,
            ):
                raise ValueError(
                    "message from_self is invalid"
                )

            if (
                since_ms is not None
                and timestamp_ms <= since_ms
            ):
                continue

            item = {
                "content": content,
                "from_self": from_self,
                "timestamp_ms":
                    timestamp_ms,
            }

            if not from_self:
                sender = message.get(
                    "sender"
                )

                if isinstance(
                    sender,
                    str,
                ) and sender:
                    item["sender"] = sender

            result.append(item)

        result.sort(
            key=lambda item:
                item["timestamp_ms"]
        )

        if since_ms is None:
            result = result[-MAX_MESSAGES:]

        return {
            "available": True,
            "convo_id": convo_id,
            "messages": result,
        }


class UnavailableChatReader:
    """Chat is unavailable when native IPC is not attached."""

    @staticmethod
    def read_conversations():
        return {
            "available": False,
            "reason": "native_chat_unavailable",
        }

    @staticmethod
    def read_messages(*args, **kwargs):
        return {
            "available": False,
            "reason": "native_chat_unavailable",
        }
