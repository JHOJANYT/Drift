#pragma once

#include "engine/DriftPluginInterface.h"

#include <QObject>

// Minimal working example: a plugin that just logs when it loads and unloads. Copy this folder
// as the starting point for a real one — swap the id/strings, add whatever the plugin actually
// does inside init(), and build it as a shared library (see CMakeLists.txt in this folder).
class HelloPlugin : public QObject, public drift::plugin::IDriftPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DriftPluginInterface_iid)
    Q_INTERFACES(drift::plugin::IDriftPlugin)

public:
    int abiVersion() const override { return kDriftPluginAbiVersion; }
    QString id() const override { return QStringLiteral("com.example.hello"); }
    QString displayName() const override { return QStringLiteral("Hello Plugin"); }
    QString version() const override { return QStringLiteral("1.0.0"); }
    QString author() const override { return QStringLiteral("Your Name"); }

    bool init(const drift::plugin::DriftHostContext &context, QString *error) override
    {
        Q_UNUSED(error);
        if (context.log)
            context.log(id(), QStringLiteral("initialized — data dir: %1").arg(context.pluginDataDir));
        // Real plugins: register QML types on context.qmlEngine, connect to signals on
        // context.appController, etc. from here.
        return true;
    }

    void shutdown() override
    {
        // Release everything acquired in init().
    }
};
