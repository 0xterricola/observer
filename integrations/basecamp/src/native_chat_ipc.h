#pragma once

class ChatModule;

// Serves read-only native Chat requests over an existing Unix socket.
// The caller owns the file descriptor and must close it after shutdown.
void serveNativeChatIpc(int fd, ChatModule& chat);
