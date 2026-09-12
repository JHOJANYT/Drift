#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QtPlugin>

// Contract for third-party native plugins (.so / .dll / .dylib built against this header).
//
// A plugin is a shared library exporting one QObject that implements IDriftPlugin via
// Q_INTERFACES + Q_PLUGIN_METADATA. NativePluginManager finds it with QPluginLoader, checks
// abiVersion() before touching anything else, and only calls init() if it matches this build's
// kDriftPluginAbiVersion. There is no compatibility shim across ABI versions: a mismatch means
// "rebuild against this SDK", not "load and hope".
//
// Bump kDriftPluginAbiVersion whenever DriftHostContext's shape changes, a virtual is added or
// reordered in IDriftPlugin, or anything else could make an old binary misread the new layout.
inline constexpr int kDriftPluginAbiVersion = 1;

class AppController; // src/models/AppController.h — the "AppController"/"EditorState" QML singleton
class QQmlApplicationEngine;

namespace drift::plugin {

// What a plugin is handed at init(). Everything here is a borrowed pointer owned by the host;
// a plugin must not delete any of them and must not use them past shutdown().
struct DriftHostContext
{
    int abiVersion = kDriftPluginAbiVersion;
    AppController *appController = nullptr;      // timeline/project API (Q_INVOKABLE surface)
    QQmlApplicationEngine *qmlEngine = nullptr;   // to register plugin QML types/singletons
    QString pluginDataDir;                        // <AppDataLocation>/plugins/native/<id>/data
    // Host-side logging so plugin messages land in the same log/console as the app's own,
    // tagged with the plugin id, instead of raw qDebug with no attribution.
    void (*log)(const QString &pluginId, const QString &message) = nullptr;
};

class IDriftPlugin
{
public:
    virtual ~IDriftPlugin() = default;

    // Must return kDriftPluginAbiVersion of the SDK this plugin was BUILT against. Checked
    // before init() is called; a mismatch is refused, not coerced.
    virtual int abiVersion() const = 0;

    virtual QString id() const = 0;          // stable, reverse-DNS-ish: "com.someone.myplugin"
    virtual QString displayName() const = 0;
    virtual QString version() const = 0;     // semver, shown in the plugin manager UI
    virtual QString author() const = 0;

    // Called once, on the main thread, after the QML engine exists but before the first window
    // is shown. Return false (and set *error) to abort loading this plugin only — the host and
    // other plugins keep running.
    virtual bool init(const DriftHostContext &context, QString *error) = 0;

    // Called once at app shutdown, or when the user disables the plugin at runtime. Must release
    // everything init() acquired; the plugin may be unloaded from memory right after this returns.
    virtual void shutdown() = 0;

    // Optional: arbitrary settings the plugin manager UI can show/edit as raw JSON without
    // needing to know the plugin's internal shape. Both are optional no-ops by default.
    virtual QJsonObject settingsSchema() const { return {}; }
    virtual void applySettings(const QJsonObject & /*settings*/) {}
};

} // namespace drift::plugin

#define DriftPluginInterface_iid "org.cutwire.Drift.IDriftPlugin/1.0"
Q_DECLARE_INTERFACE(drift::plugin::IDriftPlugin, DriftPluginInterface_iid)
