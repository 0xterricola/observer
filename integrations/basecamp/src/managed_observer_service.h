#pragma once

#include <mutex>
#include <string>
#include <thread>

struct ManagedObserverResult {
    bool ok;
    std::string state;
    std::string detail;
    int pid;
};

class ManagedObserverService {
public:
    ~ManagedObserverService();

    ManagedObserverResult start(
        const std::string& executablePath,
        const std::string& runtimeDir
    );

    ManagedObserverResult status();
    ManagedObserverResult stop();

private:
    bool runningLocked();
    void stopNativeIpcLocked();

    std::mutex mutex_;
    int pid_ = -1;
    std::string lastFailure_;
    int nativeIpcFd_ = -1;
    std::thread nativeIpcWorker_;
};
