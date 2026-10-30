#include <QDebug>
#include "ObserverManagerBackend.h"
#include "third_party/qrcodegen.hpp"

#include <QByteArray>
#include <QStringList>
#include <exception>

#include <logos_sdk.h>

#include <QJsonDocument>
#include <QDateTime>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>


namespace {

QString makePairingQrMatrix(const QString& uri)
{
    if (uri.isEmpty())
        return {};

    try {
        const QByteArray utf8 = uri.toUtf8();

        const auto qr = qrcodegen::QrCode::encodeText(
            utf8.constData(),
            qrcodegen::QrCode::Ecc::MEDIUM
        );

        QStringList rows;

        for (int y = 0; y < qr.getSize(); ++y) {
            QString row;
            row.reserve(qr.getSize());

            for (int x = 0; x < qr.getSize(); ++x) {
                row.append(
                    qr.getModule(x, y)
                        ? QLatin1Char('1')
                        : QLatin1Char('0')
                );
            }

            rows.append(row);
        }

        return rows.join(QLatin1Char('\n'));
    } catch (const std::exception&) {
        return {};
    }
}

}

ObserverManagerBackend::ObserverManagerBackend(
    LogosAPI* api,
    QObject* parent)
    : ObserverManagerBackendSimpleSource(parent),
      m_api(api ? api : new LogosAPI("observer_ui", this)),
      m_modules(new LogosModules(m_api))
{
    setReady(true);
    setServiceBusy(false);
    setServiceStatus(QStringLiteral("Not checked"));
    setServiceDetails(QStringLiteral(
        "Managed Observer status has not been checked."));

    setPairingBusy(false);
    setPairingStatus(QStringLiteral("Not paired"));
    setPairingUri(QString());
    setPairingQrMatrix(QString());
    setPairingExpiresAt(0);

    m_pairingExpiryTimer.setSingleShot(true);

    connect(
        &m_pairingExpiryTimer,
        &QTimer::timeout,
        this,
        [this]() {
            setPairingUri(QString());
            setPairingQrMatrix(QString());
            setPairingExpiresAt(0);
            setPairingStatus(QStringLiteral("Offer expired"));
        });

    setHealthBusy(false);
    setChatBusy(false);
    setNativeChatBusy(false);
    setNativeChatStatus(QStringLiteral("Not tested"));
    setNativeChatDetails(QStringLiteral(
        "Native Chat has not been tested."));
    setChatBridgeStatus(QStringLiteral("Not checked"));
    setChatStatus(QStringLiteral("Not checked"));
    setConversationsStatus(QStringLiteral("Not checked"));
    setChatDetails(QString());
    setHealthStatus(QStringLiteral("Not checked"));
    setHealthDetails(QStringLiteral(
        "Configure the Observer connection and check its health."));

    setDeviceBusy(false);
    setDeviceStatus(QStringLiteral("Not checked"));
    setDeviceDetails(QStringLiteral(
        "Device authorization has not been checked."));
}

ObserverManagerBackend::~ObserverManagerBackend()
{
    delete m_modules;
}

void ObserverManagerBackend::checkHealth(
    QString endpoint,
    QString caCert,
    QString spkiPin)
{
    if (healthBusy())
        return;

    endpoint = endpoint.trimmed();
    caCert = caCert.trimmed();
    spkiPin = spkiPin.trimmed();

    // Empty fields select managed Observer discovery.
    // Explicit connection details retain strict manual verification.
    const bool managedCheck =
        endpoint.isEmpty() &&
        caCert.isEmpty() &&
        spkiPin.isEmpty();

    if (!managedCheck &&
        (endpoint.isEmpty() ||
         caCert.isEmpty() ||
         spkiPin.isEmpty())) {
        setHealthStatus(QStringLiteral("Configuration required"));
        setHealthDetails(QStringLiteral(
            "Provide all three connection fields, or clear "
            "all three to verify the managed Observer."));
        return;
    }

    QJsonObject config;

    if (managedCheck) {
        config.insert(QStringLiteral("managed"), true);
    } else {
        config.insert(QStringLiteral("url"), endpoint);
        config.insert(QStringLiteral("ca_cert"), caCert);
        config.insert(QStringLiteral("spki_pin"), spkiPin);
    }

    const QString configJson = QString::fromUtf8(
        QJsonDocument(config).toJson(QJsonDocument::Compact));

    setHealthBusy(true);
    setHealthStatus(QStringLiteral("Checking..."));
    setHealthDetails(QString());

    QPointer<ObserverManagerBackend> guard(this);

    m_modules->observer.getHealthAsyncResult(
        configJson,
        [guard](logos::AsyncResult<QString> outcome) {
            const bool succeeded = outcome.ok();
            const QString result = outcome.value;

            if (!guard)
                return;

            QMetaObject::invokeMethod(
                guard.data(),
                [guard, succeeded, result]() {
                    if (!guard)
                        return;

                    guard->setHealthBusy(false);

                    if (!succeeded) {
                        guard->setHealthStatus(
                            QStringLiteral("Module call failed"));
                        guard->setHealthDetails(
                            QStringLiteral(
                                "Could not retrieve Observer health."));
                        return;
                    }

                    QJsonParseError error;
                    const QJsonDocument document =
                        QJsonDocument::fromJson(
                            result.toUtf8(), &error);

                    if (error.error != QJsonParseError::NoError ||
                        !document.isObject()) {
                        guard->setHealthStatus(
                            QStringLiteral("Invalid response"));
                        guard->setHealthDetails(result);
                        return;
                    }

                    const QJsonObject response = document.object();
                    const QJsonObject observer =
                        response.value("observer").toObject();

                    const bool healthy =
                        observer.value("healthy").toBool() &&
                        observer.value("identity_verified").toBool();

                    guard->setHealthStatus(
                        healthy
                            ? QStringLiteral("Healthy")
                            : QStringLiteral("Unavailable or unhealthy"));

                    guard->setHealthDetails(
                        QString::fromUtf8(
                            document.toJson(QJsonDocument::Indented)));
                },
                Qt::QueuedConnection);
        });
}



void ObserverManagerBackend::checkDeviceSummary()
{
    if (deviceBusy())
        return;

    setDeviceBusy(true);
    setDeviceStatus(QStringLiteral("Checking..."));
    setDeviceDetails(QStringLiteral(
        "Checking local device authorization."));

    QPointer<ObserverManagerBackend> guard(this);

    m_modules->observer.getDeviceSummaryAsyncResult(
        [guard](logos::AsyncResult<QString> outcome) {
            const bool succeeded = outcome.ok();
            const QString payload = outcome.value;

            if (!guard)
                return;

            QMetaObject::invokeMethod(
                guard.data(),
                [guard, succeeded, payload]() {
                    if (!guard)
                        return;

                    guard->setDeviceBusy(false);

                    if (!succeeded) {
                        guard->setDeviceStatus(
                            QStringLiteral("Error"));
                        guard->setDeviceDetails(
                            QStringLiteral(
                                "Could not retrieve device authorization."));
                        return;
                    }

                    QJsonParseError error;
                    const QJsonDocument document =
                        QJsonDocument::fromJson(
                            payload.toUtf8(), &error);

                    if (error.error != QJsonParseError::NoError ||
                        !document.isObject()) {
                        guard->setDeviceStatus(
                            QStringLiteral("Error"));
                        guard->setDeviceDetails(
                            QStringLiteral(
                                "Invalid device authorization response."));
                        return;
                    }

                    const QJsonObject response = document.object();

                    if (!response.value("ok").toBool()) {
                        guard->setDeviceStatus(
                            QStringLiteral("Error"));
                        guard->setDeviceDetails(
                            QStringLiteral(
                                "Authorization records could not be verified."));
                        return;
                    }

                    const int authorized =
                        response.value("authorized_devices").toInt(-1);

                    const int chatRead =
                        response.value("chat_read_devices").toInt(-1);

                    const QString status =
                        response.value("status").toString();

                    if (authorized < 0 || chatRead < 0 ||
                        chatRead > authorized ||
                        (status != QStringLiteral("ready") &&
                         status != QStringLiteral("unpaired")) ||
                        (status == QStringLiteral("ready")) !=
                            (chatRead > 0)) {
                        guard->setDeviceStatus(
                            QStringLiteral("Error"));
                        guard->setDeviceDetails(
                            QStringLiteral(
                                "Invalid device authorization counts."));
                        return;
                    }

                    if (chatRead > 0) {
                        guard->setDeviceStatus(
                            QStringLiteral("Ready"));
                        guard->setDeviceDetails(
                            QStringLiteral(
                                "%1 device(s) authorized for chat.read")
                                .arg(chatRead));
                    } else {
                        guard->setDeviceStatus(
                            QStringLiteral("Unpaired"));
                        guard->setDeviceDetails(
                            QStringLiteral(
                                "No device authorized for Chat. "
                                "Use Pair Device to connect one."));
                    }
                },
                Qt::QueuedConnection);
        });
}

void ObserverManagerBackend::checkChatHealth()
{
    if (chatBusy())
        return;

    setChatBusy(true);
    setChatBridgeStatus(QStringLiteral("Checking..."));
    setChatStatus(QStringLiteral("Checking..."));
    setConversationsStatus(QStringLiteral("Checking..."));
    setChatDetails(QString());

    QPointer<ObserverManagerBackend> guard(this);

    m_modules->observer.getChatHealthAsyncResult(
        [guard](logos::AsyncResult<QString> outcome) {
            const bool succeeded = outcome.ok();
            const QString result = outcome.value;

            if (!guard)
                return;

            QMetaObject::invokeMethod(
                guard.data(),
                [guard, succeeded, result]() {
                    if (!guard)
                        return;

                    guard->setChatBusy(false);

                    if (!succeeded) {
                        guard->setChatBridgeStatus(
                            QStringLiteral("Unknown"));
                        guard->setChatStatus(
                            QStringLiteral("Unknown"));
                        guard->setConversationsStatus(
                            QStringLiteral("Unknown"));
                        guard->setChatDetails(
                            QStringLiteral("Module call failed."));
                        return;
                    }

                    QJsonParseError error;
                    const QJsonDocument document =
                        QJsonDocument::fromJson(
                            result.toUtf8(), &error);

                    if (error.error != QJsonParseError::NoError ||
                        !document.isObject()) {
                        guard->setChatBridgeStatus(
                            QStringLiteral("Unknown"));
                        guard->setChatStatus(
                            QStringLiteral("Unknown"));
                        guard->setConversationsStatus(
                            QStringLiteral("Unknown"));
                        guard->setChatDetails(
                            QStringLiteral("Invalid diagnostic response."));
                        return;
                    }

                    const QJsonObject response = document.object();
                    const bool bridge =
                        response.value("chat_bridge")
                            .toObject().value("reachable").toBool();
                    const bool chat =
                        response.value("basecamp_chat")
                            .toObject().value("available").toBool();

                    guard->setChatBridgeStatus(
                        bridge ? QStringLiteral("Responding")
                               : QStringLiteral("Unavailable"));

                    guard->setChatStatus(
                        chat ? QStringLiteral("Available")
                             : QStringLiteral("Unavailable"));

                    const QString reason =
                        response.value("reason").toString();

                    const QJsonObject conversations =
                        response.value("conversations").toObject();

                    const bool readable =
                        conversations.value("readable").toBool();

                    const int count =
                        conversations.value("count").toInt(-1);

                    if (!bridge) {
                        guard->setChatBridgeStatus(
                            reason == QStringLiteral("bridge_timeout")
                            ? QStringLiteral("Timed out")
                            : QStringLiteral("Unavailable"));
                        guard->setChatStatus(QStringLiteral("Unknown"));
                    } else if (!chat) {
                        guard->setChatStatus(
                            reason == QStringLiteral("chat_not_started")
                            ? QStringLiteral("Not started")
                            : reason == QStringLiteral("chat_rpc_timeout")
                            ? QStringLiteral("Timed out")
                            : QStringLiteral("Unknown"));
                    }

                    if (chat && readable && count >= 0) {
                        guard->setConversationsStatus(
                            QStringLiteral("Readable (%1)").arg(count));
                    } else if (!chat) {
                        guard->setConversationsStatus(
                            QStringLiteral("Not tested"));
                    } else {
                        guard->setConversationsStatus(
                            reason == QStringLiteral("conversations_timeout")
                            ? QStringLiteral("Timed out")
                            : QStringLiteral("Unavailable"));
                    }

                    guard->setChatDetails(reason);
                },
                Qt::QueuedConnection);
        });
}



void ObserverManagerBackend::checkNativeChatStatus()
{
    if (nativeChatBusy())
        return;

    setNativeChatBusy(true);
    setNativeChatStatus(QStringLiteral("Checking..."));
    setNativeChatDetails(QString());

    QPointer<ObserverManagerBackend> guard(this);

    m_modules->observer.getNativeChatStatusAsyncResult(
        [guard](logos::AsyncResult<QString> outcome) {
            const bool succeeded = outcome.ok();
            const QString payload = outcome.value;

            if (!guard)
                return;

            QMetaObject::invokeMethod(
                guard.data(),
                [guard, succeeded, payload]() {
                    if (!guard)
                        return;

                    guard->setNativeChatBusy(false);

                    if (!succeeded) {
                        guard->setNativeChatStatus(
                            QStringLiteral("Module call failed"));
                        guard->setNativeChatDetails(
                            QStringLiteral(
                                "Could not invoke the native Observer method."));
                        return;
                    }

                    QJsonParseError error;
                    const QJsonDocument document =
                        QJsonDocument::fromJson(
                            payload.toUtf8(), &error);

                    if (error.error != QJsonParseError::NoError ||
                        !document.isObject()) {
                        guard->setNativeChatStatus(
                            QStringLiteral("Invalid response"));
                        guard->setNativeChatDetails(payload);
                        return;
                    }

                    const QJsonObject response = document.object();
                    const bool valid =
                        response.value("source").toString()
                            == QStringLiteral("native_sdk") &&
                        response.value("ok").isBool();

                    guard->setNativeChatStatus(
                        !valid
                            ? QStringLiteral("Invalid response")
                            : response.value("ok").toBool()
                                ? QStringLiteral("Connected")
                                : QStringLiteral("Unavailable"));

                    guard->setNativeChatDetails(
                        QString::fromUtf8(
                            document.toJson(QJsonDocument::Indented)));
                },
                Qt::QueuedConnection);
        });
}

void ObserverManagerBackend::applyServiceResult(
    bool succeeded,
    const QString& payload)
{
    setServiceBusy(false);

    if (!succeeded) {
        setServiceStatus(QStringLiteral("Module call failed"));
        setServiceDetails(QStringLiteral(
            "Could not communicate with the native Observer module."));
        return;
    }

    QJsonParseError error;
    const QJsonDocument document =
        QJsonDocument::fromJson(payload.toUtf8(), &error);

    if (error.error != QJsonParseError::NoError ||
        !document.isObject()) {
        setServiceStatus(QStringLiteral("Invalid response"));
        setServiceDetails(payload);
        return;
    }

    const QJsonObject response = document.object();
    const QString state = response.value("state").toString();

    setServiceStatus(
        state.isEmpty() ? QStringLiteral("Unknown") : state);

    setServiceDetails(response.value("detail").toString());
}


void ObserverManagerBackend::refreshServiceStatus()
{
    if (serviceBusy())
        return;

    setServiceBusy(true);
    setServiceStatus(QStringLiteral("Checking..."));
    setServiceDetails(QString());

    QPointer<ObserverManagerBackend> guard(this);

    m_modules->observer.getServiceStatusAsyncResult(
        [guard](logos::AsyncResult<QString> outcome) {
            const bool succeeded = outcome.ok();
            const QString payload = outcome.value;

            if (!guard)
                return;

            QMetaObject::invokeMethod(
                guard.data(),
                [guard, succeeded, payload]() {
                    if (guard)
                        guard->applyServiceResult(
                            succeeded, payload);
                },
                Qt::QueuedConnection);
        });
}


void ObserverManagerBackend::startService()
{
    if (serviceBusy())
        return;

    setServiceBusy(true);
    setServiceStatus(QStringLiteral("Starting..."));
    setServiceDetails(QString());

    QPointer<ObserverManagerBackend> guard(this);

    m_modules->observer.startServiceAsyncResult(
        [guard](logos::AsyncResult<QString> outcome) {
            const bool succeeded = outcome.ok();
            const QString payload = outcome.value;

            if (!guard)
                return;

            QMetaObject::invokeMethod(
                guard.data(),
                [guard, succeeded, payload]() {
                    if (guard)
                        guard->applyServiceResult(
                            succeeded, payload);
                },
                Qt::QueuedConnection);
        });
}


void ObserverManagerBackend::stopService()
{
    if (serviceBusy())
        return;

    setServiceBusy(true);
    setServiceStatus(QStringLiteral("Stopping..."));
    setServiceDetails(QString());

    QPointer<ObserverManagerBackend> guard(this);

    m_modules->observer.stopServiceAsyncResult(
        [guard](logos::AsyncResult<QString> outcome) {
            const bool succeeded = outcome.ok();
            const QString payload = outcome.value;

            if (!guard)
                return;

            QMetaObject::invokeMethod(
                guard.data(),
                [guard, succeeded, payload]() {
                    if (guard)
                        guard->applyServiceResult(
                            succeeded, payload);
                },
                Qt::QueuedConnection);
        });
}


void ObserverManagerBackend::createPairingOffer()
{
    if (pairingBusy() || serviceBusy() ||
        serviceStatus() != QStringLiteral("running"))
        return;

    m_pairingExpiryTimer.stop();
    setPairingUri(QString());
    setPairingQrMatrix(QString());
    setPairingExpiresAt(0);
    setPairingBusy(true);
    setPairingStatus(QStringLiteral("Creating offer..."));

    QPointer<ObserverManagerBackend> guard(this);

    m_modules->observer.createPairingOfferAsyncResult(
        [guard](logos::AsyncResult<QString> outcome) {
            const bool succeeded = outcome.ok();
            const QString payload = outcome.value;

            if (!guard)
                return;

            QMetaObject::invokeMethod(
                guard.data(),
                [guard, succeeded, payload]() {
                    if (!guard)
                        return;

                    guard->setPairingBusy(false);

                    if (!succeeded) {
                        guard->setPairingStatus(
                            QStringLiteral("Module call failed"));
                        return;
                    }

                    QJsonParseError parseError;
                    const QJsonDocument document =
                        QJsonDocument::fromJson(
                            payload.toUtf8(), &parseError);

                    if (parseError.error !=
                            QJsonParseError::NoError ||
                        !document.isObject()) {
                        guard->setPairingStatus(
                            QStringLiteral("Invalid response"));
                        return;
                    }

                    const QJsonObject root = document.object();

                    if (!root.value("ok").toBool()) {
                        const QString error =
                            root.value("error").toString();

                        guard->setPairingStatus(
                            error == QStringLiteral("forbidden")
                                ? QStringLiteral("Authorization denied")
                                : QStringLiteral("Pairing unavailable"));
                        return;
                    }

                    const QJsonObject offer =
                        root.value("offer").toObject();

                    const QString uri =
                        offer.value("qr").toString();

                    const QString endpoint =
                        offer.value("endpoint").toString();

                    const qint64 expiresAt = static_cast<qint64>(
                        offer.value("expires_at").toDouble());

                    const qint64 secondsRemaining =
                        expiresAt - QDateTime::currentSecsSinceEpoch();

                    if (uri.isEmpty() ||
                        endpoint.isEmpty() ||
                        secondsRemaining <= 0 ||
                        secondsRemaining > 300) {
                        guard->setPairingStatus(
                            QStringLiteral("Invalid or expired offer"));
                        return;
                    }

                    // The URI contains a temporary pairing secret.
                    // Never print it to logs or diagnostic messages.
                    const QString matrix = makePairingQrMatrix(uri);

                    if (matrix.isEmpty()) {
                        guard->setPairingStatus(
                            QStringLiteral("QR encoding failed"));
                        return;
                    }

                    guard->setPairingQrMatrix(matrix);
                    guard->setPairingExpiresAt(expiresAt);
                    guard->setPairingUri(uri);
                    guard->setPairingStatus(
                        QStringLiteral("Offer ready (%1)").arg(endpoint));

                    guard->m_pairingExpiryTimer.start(
                        static_cast<int>(secondsRemaining * 1000));
                },
                Qt::QueuedConnection);
        });
}
