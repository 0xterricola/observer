#include "native_chat_dispatch.h"

#include "chat_module_api.h"
#include <nlohmann/json.hpp>

#include <string>

namespace {

using json = nlohmann::json;

constexpr std::size_t MAX_RESPONSE = 8 * 1024 * 1024;
constexpr int CALL_TIMEOUT_MS = 2500;

std::string fail(const char* reason) {
    return json{
        {"ok", false},
        {"error", reason}
    }.dump();
}

std::string success(const json& result) {
    const std::string body = json{
        {"ok", true},
        {"result", result}
    }.dump();

    if (body.size() > MAX_RESPONSE)
        return fail("response_too_large");

    return body;
}

json conversationJson(const ChatModule::Conversation& c) {
    json value = {
        {"convo_id", c.convo_id},
        {"message_count", c.message_count},
        {"last_activity_ms", c.last_activity_ms},
        {"kind", c.kind},
        {"history_only", c.history_only}
    };

    if (c.nickname) value["nickname"] = *c.nickname;
    if (c.name) value["name"] = *c.name;
    if (c.description) value["description"] = *c.description;
    if (c.preview) value["preview"] = *c.preview;

    return value;
}

json messageJson(const ChatModule::Message& m) {
    json value = {
        {"content", m.content},
        {"timestamp_ms", m.timestamp_ms},
        {"from_self", m.from_self}
    };

    if (m.sender) value["sender"] = *m.sender;

    return value;
}

} // namespace

std::string dispatchNativeChat(
    ChatModule& chat,
    const std::string& requestText
) {
    try {
        const auto request =
            json::parse(requestText, nullptr, false);

        if (!request.is_object() ||
            !request.contains("method") ||
            !request["method"].is_string()) {
            return fail("invalid_request");
        }

        const std::string method =
            request["method"].get<std::string>();

        // Strict allowlist. Never invoke write/lifecycle methods.
        if (method != "get_address" &&
            method != "list_conversations" &&
            method != "get_messages") {
            return fail("method_not_permitted");
        }

        logos::CallError error;

        if (method == "get_address") {
            if (request.size() != 1)
                return fail("invalid_parameters");

            const auto address =
                chat.get_address(&error, CALL_TIMEOUT_MS);

            if (!error.code.empty())
                return fail("chat_call_failed");

            return success(address);
        }

        if (method == "list_conversations") {
            if (request.size() != 1)
                return fail("invalid_parameters");

            const auto conversations =
                chat.list_conversations(
                    &error, CALL_TIMEOUT_MS);

            if (!error.code.empty())
                return fail("chat_call_failed");

            json result = json::array();

            for (const auto& conversation : conversations)
                result.push_back(conversationJson(conversation));

            return success(result);
        }

        // get_messages requires exactly one nonempty convo_id.
        if (request.size() != 2 ||
            !request.contains("params") ||
            !request["params"].is_object()) {
            return fail("invalid_parameters");
        }

        const auto& params = request["params"];

        if (params.size() != 1 ||
            !params.contains("convo_id") ||
            !params["convo_id"].is_string()) {
            return fail("invalid_parameters");
        }

        const auto convoId =
            params["convo_id"].get<std::string>();

        if (convoId.empty())
            return fail("invalid_parameters");

        const auto messages =
            chat.get_messages(
                convoId, &error, CALL_TIMEOUT_MS);

        if (!error.code.empty())
            return fail("chat_call_failed");

        json result = json::array();

        for (const auto& message : messages)
            result.push_back(messageJson(message));

        return success(result);

    } catch (...) {
        // No sensitive Chat data or SDK errors in failure responses.
        return fail("native_chat_dispatch_failed");
    }
}
