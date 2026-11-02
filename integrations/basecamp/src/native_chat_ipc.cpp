#include "native_chat_ipc.h"
#include "native_chat_dispatch.h"

#include <arpa/inet.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstdint>
#include <string>

namespace {

constexpr std::uint32_t MAX_FRAME = 8 * 1024 * 1024;

bool readFully(int fd, void* buffer, std::size_t size) {
    auto* data = static_cast<char*>(buffer);
    std::size_t offset = 0;

    while (offset < size) {
        const ssize_t count = recv(
            fd, data + offset, size - offset, 0);

        if (count < 0 && errno == EINTR)
            continue;

        if (count <= 0)
            return false;

        offset += static_cast<std::size_t>(count);
    }

    return true;
}

bool writeFully(int fd, const void* buffer, std::size_t size) {
    const auto* data = static_cast<const char*>(buffer);
    std::size_t offset = 0;

#ifdef MSG_NOSIGNAL
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif

    while (offset < size) {
        const ssize_t count = send(
            fd, data + offset, size - offset, flags);

        if (count < 0 && errno == EINTR)
            continue;

        if (count <= 0)
            return false;

        offset += static_cast<std::size_t>(count);
    }

    return true;
}

} // namespace

void serveNativeChatIpc(int fd, ChatModule& chat) {
#ifdef SO_NOSIGPIPE
    int enabled = 1;
    if (setsockopt(
            fd, SOL_SOCKET, SO_NOSIGPIPE,
            &enabled, sizeof(enabled)) != 0)
        return;
#endif

    try {
        for (;;) {
            std::uint32_t networkLength = 0;

            if (!readFully(
                    fd, &networkLength, sizeof(networkLength)))
                break;

            const std::uint32_t length =
                ntohl(networkLength);

            if (length == 0 || length > MAX_FRAME)
                break;

            std::string request(length, '\0');

            if (!readFully(fd, request.data(), length))
                break;

            const std::string response =
                dispatchNativeChat(chat, request);

            if (response.empty() ||
                response.size() > MAX_FRAME)
                break;

            const std::uint32_t responseLength =
                htonl(static_cast<std::uint32_t>(
                    response.size()));

            if (!writeFully(
                    fd, &responseLength, sizeof(responseLength)))
                break;

            if (!writeFully(
                    fd, response.data(), response.size()))
                break;
        }
    } catch (...) {
        // A malformed or failed IPC session closes at the caller.
    }
}
