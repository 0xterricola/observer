#pragma once

#include <string>

class ChatModule;

// Private IPC dispatcher; not exported as an Observer module method.
std::string dispatchNativeChat(
    ChatModule& chat,
    const std::string& request
);
