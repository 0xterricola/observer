#include "managed_observer_service.h"
#include "observer_network_config.h"
#include "native_chat_ipc.h"
#include "chat_module_api.h"

#include <cstdlib>
#include <filesystem>
#include <vector>

#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/socket.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef _WIN32
extern char **environ;
#endif

namespace fs = std::filesystem;

ManagedObserverService::~ManagedObserverService() {
    stop();
}

void ManagedObserverService::stopNativeIpcLocked() {
#ifndef _WIN32
    if (nativeIpcFd_ >= 0)
        ::shutdown(nativeIpcFd_, SHUT_RDWR);

    if (nativeIpcWorker_.joinable())
        nativeIpcWorker_.join();

    if (nativeIpcFd_ >= 0)
        ::close(nativeIpcFd_);

    nativeIpcFd_ = -1;
#endif
}

bool ManagedObserverService::runningLocked() {
#ifndef _WIN32
    if (pid_ <= 0) return false;

    int childStatus = 0;
    pid_t result;
    do {
        result = waitpid(pid_, &childStatus, WNOHANG);
    } while (result < 0 && errno == EINTR);

    if (result == 0) return true;

    const int previousPid = pid_;
    pid_ = -1;

    if (result == previousPid) {
        if (WIFEXITED(childStatus)) {
            lastFailure_ = "Observer exited unexpectedly (code " +
                std::to_string(WEXITSTATUS(childStatus)) + ")";
        } else if (WIFSIGNALED(childStatus)) {
            lastFailure_ = "Observer terminated unexpectedly (signal " +
                std::to_string(WTERMSIG(childStatus)) + ")";
        }
    } else {
        lastFailure_ = "Unable to verify managed child process";
    }
#endif
    return false;
}

ManagedObserverResult ManagedObserverService::status() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (runningLocked()) {
        return {true, "running", "Managed process is alive", pid_};
    }

    stopNativeIpcLocked();

    if (!lastFailure_.empty()) {
        return {false, "error", lastFailure_, 0};
    }
    return {true, "stopped", "No managed process", 0};
}

ManagedObserverResult ManagedObserverService::start(
    const std::string& executablePath,
    const std::string& runtimeDir
) {
    std::lock_guard<std::mutex> lock(mutex_);

#ifdef _WIN32
    return {false, "unsupported", "Windows launcher not implemented", 0};
#else
    if (runningLocked()) {
        return {false, "running", "Observer already started", pid_};
    }

    stopNativeIpcLocked();
    lastFailure_.clear();

    const fs::path executable(executablePath);
    const bool pythonMode = !runtimeDir.empty();

    if (!executable.is_absolute() ||
        !fs::is_regular_file(executable) ||
        access(executable.c_str(), X_OK) != 0) {
        return {
            false, "error",
            "Observer executable missing or not executable: " +
                executable.string(),
            0
        };
    }

    fs::path script;

    if (pythonMode) {
        script = fs::path(runtimeDir) / "v2_server.py";

        if (!script.is_absolute() || !fs::is_regular_file(script)) {
            return {
                false, "error",
                "Invalid absolute Observer runtime directory",
                0
            };
        }
    }

    const char* home = std::getenv("HOME");
    if (!home || !*home) {
        return {false, "error", "HOME is not configured", 0};
    }

    const fs::path stateDir =
        fs::path(home) / ".local/state/logos-observer/basecamp";

    std::error_code ec;
    fs::create_directories(stateDir, ec);
    if (ec || chmod(stateDir.c_str(), 0700) != 0) {
        return {false, "error", "Unable to create private state directory", 0};
    }

    const fs::path logPath = stateDir / "observer.log";
    const int logFd = open(
        logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600
    );
    if (logFd < 0) {
        return {false, "error", "Unable to open Observer log", 0};
    }

    const auto network = observer_network::current();

    if (!network.valid) {
        ::close(logFd);
        return {
            false,
            "not_configured",
            "LOGOS_OBSERVER_LAN_IP must be a private IPv4 address",
            0
        };
    }

    std::vector<std::string> arguments = {
        executable.string()
    };

    if (pythonMode) {
        arguments.push_back(script.string());
    }

    arguments.insert(arguments.end(), {
        "--host", network.host,
        "--advertise-host", network.host,
        "--port", "18446",
        "--state-dir", stateDir.string(),
        "--chat-bridge", "http://127.0.0.1:8645/rpc"
    });

    // This socket pair is private to Observer Core and its child.
    int ipcSockets[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, ipcSockets) != 0) {
        ::close(logFd);
        return {false, "error", "Unable to create native Chat IPC", 0};
    }

    // Never leak the parent endpoint into the exec'd Python child.
    const int flags = ::fcntl(ipcSockets[0], F_GETFD);
    if (flags < 0 ||
        ::fcntl(ipcSockets[0], F_SETFD, flags | FD_CLOEXEC) != 0) {
        ::close(ipcSockets[0]);
        ::close(ipcSockets[1]);
        ::close(logFd);
        return {false, "error", "Unable to secure native Chat IPC", 0};
    }

    arguments.push_back("--chat-native-fd");
    arguments.push_back(std::to_string(ipcSockets[1]));

    std::vector<char*> argv;
    for (auto& argument : arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    // Spawn directly instead of forking the multithreaded module host.
    posix_spawn_file_actions_t actions;
    int spawnError = posix_spawn_file_actions_init(&actions);

    if (spawnError != 0) {
        ::close(logFd);
        ::close(ipcSockets[0]);
        ::close(ipcSockets[1]);
        return {false, "error", "Unable to initialize spawn actions", 0};
    }

    // Preserve the child socket explicitly, and redirect its logs.
    spawnError = posix_spawn_file_actions_adddup2(
        &actions, logFd, STDOUT_FILENO);

    if (spawnError == 0)
        spawnError = posix_spawn_file_actions_adddup2(
            &actions, logFd, STDERR_FILENO);

    if (spawnError == 0) {
#if defined(__APPLE__)
        spawnError = posix_spawn_file_actions_addinherit_np(
            &actions, ipcSockets[1]);
#else
        spawnError = posix_spawn_file_actions_adddup2(
            &actions, ipcSockets[1], ipcSockets[1]);
#endif
    }

    if (spawnError == 0)
        spawnError = posix_spawn_file_actions_addclose(
            &actions, ipcSockets[0]);

    if (spawnError == 0 && logFd > STDERR_FILENO)
        spawnError = posix_spawn_file_actions_addclose(
            &actions, logFd);

    posix_spawnattr_t attributes;
    const int attributesResult =
        posix_spawnattr_init(&attributes);

    if (spawnError == 0)
        spawnError = attributesResult;

#if defined(__APPLE__) && defined(POSIX_SPAWN_CLOEXEC_DEFAULT)
    // Inherit only descriptors explicitly selected by file actions.
    if (spawnError == 0)
        spawnError = posix_spawnattr_setflags(
            &attributes, POSIX_SPAWN_CLOEXEC_DEFAULT);
#endif

    pid_t child = -1;

    if (spawnError == 0)
        spawnError = posix_spawn(
            &child, argv[0], &actions,
            &attributes, argv.data(), environ);

    if (attributesResult == 0)
        posix_spawnattr_destroy(&attributes);

    posix_spawn_file_actions_destroy(&actions);

    ::close(logFd);
    ::close(ipcSockets[1]);

    if (spawnError != 0) {
        ::close(ipcSockets[0]);
        return {
            false, "error",
            "Unable to spawn Observer process (error " +
                std::to_string(spawnError) + ")",
            0
        };
    }

    try {
        // Construct the SDK client inside the worker thread.
        // Do not share ObserverImpl's SDK client across threads.
        nativeIpcWorker_ = std::thread([fd = ipcSockets[0]] {
            try {
                ChatModule chat("observer");
                serveNativeChatIpc(fd, chat);
            } catch (...) {
                // The parent owns and closes the descriptor.
            }
        });
        nativeIpcFd_ = ipcSockets[0];
    } catch (...) {
        ::close(ipcSockets[0]);
        ::kill(child, SIGTERM);
        int status = 0;
        while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        return {false, "error", "Unable to start native Chat worker", 0};
    }

    pid_ = static_cast<int>(child);

    // Detect immediate interpreter and startup failures.
    for (int attempt = 0; attempt < 10; ++attempt) {
        usleep(100000);
        if (!runningLocked()) {
            stopNativeIpcLocked();
            return {false, "error", lastFailure_, 0};
        }
    }

    return {true, "starting", "Observer process launched", pid_};
#endif
}

ManagedObserverResult ManagedObserverService::stop() {
    std::lock_guard<std::mutex> lock(mutex_);

#ifdef _WIN32
    return {false, "unsupported", "Windows launcher not implemented", 0};
#else
    if (!runningLocked()) {
        stopNativeIpcLocked();
        return {true, "stopped", "Observer already stopped", 0};
    }

    if (kill(pid_, SIGTERM) != 0 && errno != ESRCH) {
        return {false, "error", "Unable to signal managed process", pid_};
    }

    // Give the server time to close cleanly and flush state.
    for (int attempt = 0; attempt < 50; ++attempt) {
        if (!runningLocked()) {
            lastFailure_.clear();
        stopNativeIpcLocked();
        return {true, "stopped", "Observer stopped gracefully", 0};
        }
        usleep(100000);
    }

    // Only signal a process still confirmed as our child.
    if (runningLocked()) {
        kill(pid_, SIGKILL);

        int childStatus = 0;
        while (waitpid(pid_, &childStatus, 0) < 0 && errno == EINTR) {
        }
        pid_ = -1;
    }

    stopNativeIpcLocked();
    lastFailure_.clear();
    return {true, "stopped", "Observer stopped after timeout", 0};
#endif
}
