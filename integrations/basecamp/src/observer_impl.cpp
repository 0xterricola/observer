#include "observer_impl.h"
#include "observer_network_config.h"
#include "observer_health.h"
#include <nlohmann/json.hpp>
#include <logos_caller.h>
#include "logos_sdk.h"
#include <cstdlib>
#include <cerrno>
#include <chrono>
#include <vector>
#include <spawn.h>
#include <poll.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__) || (defined(__linux__) && defined(__x86_64__))
#include <dlfcn.h>
#include <filesystem>
#endif

ObserverImpl::ObserverImpl() = default;

ObserverImpl::~ObserverImpl() = default;

std::string ObserverImpl::getInfo() {
    return R"json({
        "module": "observer",
        "version": "0.1.4",
        "managed_service": "manual"
    })json";
}

void ObserverImpl::onContextReady() {
    // Service management will be implemented in a later milestone.
    // Do not automatically start another Observer instance.
}

LogosShutdown ObserverImpl::aboutToUnload() {
    managedService_.stop();
    return LogosShutdown::Synchronous;
}


std::string ObserverImpl::getHealth(
    const std::string& configJson
) {
    return probeObserverHealth(configJson);
}


// Implemented in chat_health.cpp.
std::string probeChatHealth();

std::string ObserverImpl::getChatHealth() {
    return probeChatHealth();
}

std::string ObserverImpl::getNativeChatStatus() {
    try {
        logos::CallError error;
        const auto status =
            modules().chat_module.status(&error, 2500);

        if (!error.code.empty()) {
            return nlohmann::json{
                {"ok", false},
                {"source", "native_sdk"},
                {"reason", "chat_module_unavailable"}
            }.dump();
        }

        logos::CallError addressError;
        const auto address =
            modules().chat_module.get_address(&addressError, 2500);

        logos::CallError conversationsError;
        const auto conversations =
            modules().chat_module.list_conversations(
                &conversationsError, 2500);

        const bool addressOk = addressError.code.empty();
        const bool conversationsOk = conversationsError.code.empty();

        return nlohmann::json{
            {"ok", true},
            {"source", "native_sdk"},
            {"conversation_count", status.convo_count},
            {"delivery_state", status.delivery_state},
            {"delivery_adopted", status.delivery_adopted},
            {"address_call_ok", addressOk},
            {"address_available", addressOk && !address.empty()},
            {"conversations_readable", conversationsOk},
            {"listed_conversation_count",
                conversationsOk
                    ? nlohmann::json(conversations.size())
                    : nlohmann::json(nullptr)}
        }.dump();

    } catch (...) {
        return nlohmann::json{
            {"ok", false},
            {"source", "native_sdk"},
            {"reason", "native_chat_probe_failed"}
        }.dump();
    }
}



namespace {
#if defined(__APPLE__) || (defined(__linux__) && defined(__x86_64__))
int modulePathAnchor = 0;

std::string bundledObserverPath() {
    Dl_info info{};

    if (dladdr(&modulePathAnchor, &info) == 0 ||
        !info.dli_fname || !*info.dli_fname) {
        return {};
    }

    const std::filesystem::path modulePath(info.dli_fname);

    if (!modulePath.is_absolute()) {
        return {};
    }

#if defined(__APPLE__)
    constexpr const char* runtimeRelative =
        "assets/lidl/observer-runtime/portable-macos-arm64/observer";
#else
    constexpr const char* runtimeRelative =
        "assets/lidl/observer-runtime/portable-linux-x86_64/observer";
#endif

    return (modulePath.parent_path() / runtimeRelative).string();
}
#endif

std::string serviceResultJson(const ManagedObserverResult& result) {
    return nlohmann::json{
        {"ok", result.ok},
        {"state", result.state},
        {"detail", result.detail},
        {"pid", result.pid}
    }.dump();
}
}

std::string ObserverImpl::startService() {
    const char* python = std::getenv("LOGOS_OBSERVER_PYTHON");
    const char* runtime = std::getenv("LOGOS_OBSERVER_RUNTIME_DIR");

    const bool hasPython = python && *python;
    const bool hasRuntime = runtime && *runtime;

    // Preserve explicit development configuration.
    if (hasPython || hasRuntime) {
        if (!hasPython || !hasRuntime) {
            return serviceResultJson({
                false,
                "not_configured",
                "Both Observer development overrides are required",
                0
            });
        }

        return serviceResultJson(
            managedService_.start(python, runtime)
        );
    }

#if defined(__APPLE__) || (defined(__linux__) && defined(__x86_64__))
    const std::string executable = bundledObserverPath();

    if (executable.empty()) {
        return serviceResultJson({
            false,
            "not_configured",
            "Unable to locate installed Observer module",
            0
        });
    }

    return serviceResultJson(
        managedService_.start(executable, "")
    );
#else
    return serviceResultJson({
        false,
        "unsupported",
        "Bundled Observer runtime is not available on this platform",
        0
    });
#endif
}

std::string ObserverImpl::getServiceStatus() {
    return serviceResultJson(managedService_.status());
}

std::string ObserverImpl::stopService() {
    return serviceResultJson(managedService_.stop());
}

extern char **environ;


std::string ObserverImpl::getDeviceSummary() {
    const auto fail = [] {
        return nlohmann::json{
            {"ok", false},
            {"status", "error"},
            {"error", "authorization_summary_unavailable"}
        }.dump();
    };

#if defined(__APPLE__) || (defined(__linux__) && defined(__x86_64__))
    const std::string executable = bundledObserverPath();
    const char* home = std::getenv("HOME");

    if (!home || !*home || executable.empty() ||
        !std::filesystem::is_regular_file(executable))
        return fail();

    const std::string stateDir =
        (std::filesystem::path(home) /
         ".local/state/logos-observer/basecamp").string();

    std::vector<std::string> args = {
        executable,
        "pair",
        "--device-summary",
        "--state-dir", stateDir
    };

    std::vector<char*> argv;
    for (auto& arg : args)
        argv.push_back(arg.data());
    argv.push_back(nullptr);

    int fds[2];

    if (pipe(fds) != 0)
        return fail();

    posix_spawn_file_actions_t actions;

    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(fds[0]);
        close(fds[1]);
        return fail();
    }

    int setup = posix_spawn_file_actions_addclose(
        &actions, fds[0]);

    if (setup == 0)
        setup = posix_spawn_file_actions_adddup2(
            &actions, fds[1], STDOUT_FILENO);

    if (setup == 0)
        setup = posix_spawn_file_actions_addclose(
            &actions, fds[1]);

    if (setup == 0)
        setup = posix_spawn_file_actions_addopen(
            &actions, STDERR_FILENO,
            "/dev/null", O_WRONLY, 0);

    if (setup == 0)
        setup = posix_spawn_file_actions_addopen(
            &actions, STDIN_FILENO,
            "/dev/null", O_RDONLY, 0);

    pid_t child = -1;

    const int spawned = setup == 0
        ? posix_spawn(
            &child, executable.c_str(),
            &actions, nullptr, argv.data(), environ)
        : setup;

    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);

    if (spawned != 0) {
        close(fds[0]);
        return fail();
    }

    const int flags = fcntl(fds[0], F_GETFL);

    if (flags < 0 ||
        fcntl(fds[0], F_SETFL, flags | O_NONBLOCK) < 0) {
        kill(child, SIGKILL);
        int ignored = 0;
        while (waitpid(child, &ignored, 0) < 0 &&
               errno == EINTR) {}
        close(fds[0]);
        return fail();
    }

    std::string output;
    bool eof = false;
    bool reaped = false;
    bool invalid = false;
    int childStatus = 0;

    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(10);

    while (std::chrono::steady_clock::now() < deadline) {
        if (!eof) {
            pollfd descriptor{fds[0], POLLIN, 0};
            const int polled = poll(&descriptor, 1, 100);

            if (polled < 0 && errno != EINTR) {
                invalid = true;
                break;
            }

            if (polled > 0) {
                for (;;) {
                    char buffer[1024];
                    const ssize_t count =
                        read(fds[0], buffer, sizeof(buffer));

                    if (count > 0) {
                        if (output.size() +
                            static_cast<size_t>(count) > 4096) {
                            invalid = true;
                            break;
                        }
                        output.append(
                            buffer, static_cast<size_t>(count));
                    } else if (count == 0) {
                        eof = true;
                        break;
                    } else if (errno == EINTR) {
                        continue;
                    } else if (errno == EAGAIN ||
                               errno == EWOULDBLOCK) {
                        break;
                    } else {
                        invalid = true;
                        break;
                    }
                }
            }
        }

        if (invalid)
            break;

        if (!reaped) {
            const pid_t result =
                waitpid(child, &childStatus, WNOHANG);

            if (result == child)
                reaped = true;
            else if (result < 0 && errno != EINTR) {
                invalid = true;
                break;
            }
        }

        if (eof && reaped)
            break;
    }

    close(fds[0]);

    if (!reaped) {
        kill(child, SIGKILL);
        while (waitpid(child, &childStatus, 0) < 0 &&
               errno == EINTR) {}
    }

    if (invalid || !eof || !reaped ||
        !WIFEXITED(childStatus) ||
        WEXITSTATUS(childStatus) != 0)
        return fail();

    const auto result = nlohmann::json::parse(
        output, nullptr, false);

    if (!result.is_object() ||
        !result.contains("ok") ||
        result["ok"] != true ||
        !result.contains("status") ||
        !result["status"].is_string() ||
        !result.contains("authorized_devices") ||
        !result["authorized_devices"].is_number_integer() ||
        !result.contains("chat_read_devices") ||
        !result["chat_read_devices"].is_number_integer())
        return fail();

    const auto authorized =
        result["authorized_devices"].get<long long>();

    const auto chatRead =
        result["chat_read_devices"].get<long long>();

    const auto status = result["status"].get<std::string>();

    if (authorized < 0 || chatRead < 0 ||
        chatRead > authorized ||
        (status != "ready" && status != "unpaired") ||
        (status == "ready") != (chatRead > 0))
        return fail();

    return nlohmann::json{
        {"ok", true},
        {"status", status},
        {"authorized_devices", authorized},
        {"chat_read_devices", chatRead}
    }.dump();
#else
    return fail();
#endif
}

std::string ObserverImpl::createPairingOffer() {
    auto fail = [](const char* error) {
        return nlohmann::json{
            {"ok", false}, {"error", error}
        }.dump();
    };

    // Never expose pairing credentials to unrelated modules.
    if (!logos::currentCaller().isModule("observer_ui")) {
        return fail("forbidden");
    }

    const auto service = managedService_.status();
    if (!service.ok || service.state != "running") {
        return fail("observer_not_running");
    }

#if defined(__APPLE__) || (defined(__linux__) && defined(__x86_64__))
    const auto network = observer_network::current();

    if (!network.valid)
        return fail("invalid_lan_host");

    const std::string executable = bundledObserverPath();
    const char* home = std::getenv("HOME");

    if (!home || !*home || executable.empty() ||
        !std::filesystem::is_regular_file(executable)) {
        return fail("runtime_unavailable");
    }

    const std::string stateDir =
        (std::filesystem::path(home) /
         ".local/state/logos-observer/basecamp").string();

    // Fixed arguments: no shell and no untrusted input.
    std::vector<std::string> args = {
        executable,
        "pair",
        "--state-dir", stateDir,
        "--host", network.host,
        "--port", "18446",
        "--scope", "chat.read",
        "--existing-identity",
        "--no-terminal-qr",
        "--json"
    };

    std::vector<char*> argv;
    for (auto& arg : args)
        argv.push_back(arg.data());
    argv.push_back(nullptr);

    int fds[2];
    if (pipe(fds) != 0)
        return fail("pipe_failed");

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(fds[0]);
        close(fds[1]);
        return fail("spawn_setup_failed");
    }

    int setup = posix_spawn_file_actions_addclose(
        &actions, fds[0]);
    if (setup == 0)
        setup = posix_spawn_file_actions_adddup2(
            &actions, fds[1], STDOUT_FILENO);
    if (setup == 0)
        setup = posix_spawn_file_actions_addclose(
            &actions, fds[1]);
    if (setup == 0)
        setup = posix_spawn_file_actions_addopen(
            &actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    if (setup == 0)
        setup = posix_spawn_file_actions_addopen(
            &actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);

    pid_t child = -1;
    const int spawned = setup == 0
        ? posix_spawn(
            &child, executable.c_str(),
            &actions, nullptr, argv.data(), environ)
        : setup;

    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);

    if (spawned != 0) {
        close(fds[0]);
        return fail("spawn_failed");
    }

    const int flags = fcntl(fds[0], F_GETFL);
    if (flags < 0 || fcntl(
            fds[0], F_SETFL, flags | O_NONBLOCK) < 0) {
        kill(child, SIGKILL);
        int ignored = 0;
        while (waitpid(child, &ignored, 0) < 0 &&
               errno == EINTR) {}
        close(fds[0]);
        return fail("pipe_configuration_failed");
    }

    constexpr size_t maxOutput = 16384;
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(10);

    std::string output;
    bool eof = false;
    bool reaped = false;
    bool invalid = false;
    int childStatus = 0;

    while (std::chrono::steady_clock::now() < deadline) {
        if (!eof) {
            pollfd descriptor{fds[0], POLLIN, 0};
            const int polled = poll(&descriptor, 1, 100);

            if (polled < 0 && errno != EINTR) {
                invalid = true;
                break;
            }

            if (polled > 0) {
                for (;;) {
                    char buffer[4096];
                    const ssize_t count =
                        read(fds[0], buffer, sizeof(buffer));

                    if (count > 0) {
                        if (output.size() +
                            static_cast<size_t>(count) > maxOutput) {
                            invalid = true;
                            break;
                        }
                        output.append(
                            buffer, static_cast<size_t>(count));
                    } else if (count == 0) {
                        eof = true;
                        break;
                    } else if (errno == EINTR) {
                        continue;
                    } else if (errno == EAGAIN ||
                               errno == EWOULDBLOCK) {
                        break;
                    } else {
                        invalid = true;
                        break;
                    }
                }
            }
        }

        if (invalid)
            break;

        if (!reaped) {
            const pid_t result =
                waitpid(child, &childStatus, WNOHANG);
            if (result == child)
                reaped = true;
            else if (result < 0 && errno != EINTR) {
                invalid = true;
                break;
            }
        }

        if (eof && reaped)
            break;
    }

    close(fds[0]);

    if (!reaped) {
        kill(child, SIGKILL);
        while (waitpid(child, &childStatus, 0) < 0 &&
               errno == EINTR) {}
    }

    if (invalid || !eof || !reaped)
        return fail("pairing_process_failed_or_timed_out");

    if (!WIFEXITED(childStatus) ||
        WEXITSTATUS(childStatus) != 0)
        return fail("pairing_command_failed");

    // Validate before returning credentials to the authorized UI.
    const auto offer = nlohmann::json::parse(
        output, nullptr, false);

    if (!offer.is_object() ||
        !offer.contains("qr") ||
        !offer["qr"].is_string() ||
        offer["qr"].get<std::string>().empty() ||
        !offer.contains("pin") ||
        !offer["pin"].is_string() ||
        !offer.contains("expires_at") ||
        !offer["expires_at"].is_number_integer() ||
        offer.value("endpoint", std::string{}) !=
            (network.host + ":18446") ||
        !offer.contains("offered_scopes") ||
        offer["offered_scopes"] !=
            nlohmann::json::array({"chat.read"})) {
        return fail("invalid_pairing_response");
    }

    // No logging: this JSON contains a sensitive pairing URI.
    return nlohmann::json{
        {"ok", true},
        {"offer", offer}
    }.dump();
#else
    return fail("unsupported_platform");
#endif
}
