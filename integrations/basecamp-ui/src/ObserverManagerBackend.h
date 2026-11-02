#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include "rep_ObserverManagerBackend_source.h"

class LogosAPI;
struct LogosModules;

class ObserverManagerBackend
    : public ObserverManagerBackendSimpleSource
{
    Q_OBJECT

public:
    explicit ObserverManagerBackend(
        LogosAPI* api = nullptr,
        QObject* parent = nullptr);

    ~ObserverManagerBackend() override;

public slots:
    void checkHealth(
        QString endpoint,
        QString caCert,
        QString spkiPin) override;

    void checkChatHealth() override;
    void checkNativeChatStatus() override;
    void checkDeviceSummary() override;

    void refreshServiceStatus() override;
    void startService() override;
    void stopService() override;
    void createPairingOffer() override;

private:
    void applyServiceResult(
        bool succeeded,
        const QString& payload);
    LogosAPI* m_api;
    LogosModules* m_modules;
    QTimer m_pairingExpiryTimer;
};
