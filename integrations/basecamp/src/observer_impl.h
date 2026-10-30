#pragma once

#include <string>
#include "managed_observer_service.h"

#include <logos_module_context.h>

class ObserverImpl : public LogosModuleContext {
public:
    ObserverImpl();
    ~ObserverImpl();

    // Returns the module's current state.
    std::string getInfo();

    // Read-only, TLS-verified Observer health probe.
    std::string getHealth(const std::string& configJson);

    // Read-only local Basecamp Chat diagnostics.
    std::string getChatHealth();

    std::string getNativeChatStatus();

    std::string startService();
    std::string getServiceStatus();
    std::string stopService();
    std::string createPairingOffer();
    std::string getDeviceSummary();

    // Read-only local Basecamp Chat diagnostics.

protected:
    void onContextReady() override;
    LogosShutdown aboutToUnload() override;

private:
    ManagedObserverService managedService_;
};
