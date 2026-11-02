#pragma once

#include <QObject>
#include <QString>
#include <QtPlugin>

#include "ObserverManagerPluginInterface.h"
#include "LogosViewPluginBase.h"

class LogosAPI;
class ObserverManagerBackend;

class ObserverManagerPlugin
    : public QObject,
      public ObserverManagerPluginInterface,
      public ObserverManagerBackendViewPluginBase
{
    Q_OBJECT

    Q_PLUGIN_METADATA(
        IID ObserverManagerPluginInterface_iid
        FILE "../metadata.json")

    Q_INTERFACES(ObserverManagerPluginInterface LogosViewPlugin)

public:
    explicit ObserverManagerPlugin(QObject* parent = nullptr);
    ~ObserverManagerPlugin() override;

    QString name() const override
    {
        return QStringLiteral("observer_ui");
    }

    QString version() const override
    {
        return QStringLiteral("0.1.4");
    }

    Q_INVOKABLE void initLogos(LogosAPI* api);

private:
    ObserverManagerBackend* m_backend = nullptr;
};
