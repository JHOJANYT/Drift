#pragma once

#include "DriftPluginInterface.h"

#include <QObject>
#include <QPluginLoader>
#include <QString>
#include <QVariantList>

class AppController;
class QQmlApplicationEngine;

// Discovers, loads, and supervises native (.so/.dll/.dylib) community plugins.
//
// Search dir:  <AppDataLocation>/plugins/native/<id>/<lib>.(so|dll|dylib)
// Each plugin also gets a private data dir at .../<id>/data (created on demand, handed to it via
// DriftHostContext::pluginDataDir) so plugins don't scribble into each other's or the app's own
// files.
//
// Safety model — read this before wiring a "load on startup" call anywhere:
//   * A native plugin runs in-process with full memory access. It CAN crash the app, corrupt the
//     open project in memory, or do anything the app's own process can do. There is no sandbox
//     here — that would need a separate process + IPC, which is future work, not this class.
//   * Because a crash inside a plugin's init()/code can bring the whole app down before it gets a
//     chance to save anything, loading is guarded by a *crash marker*: writeCrashMarker() is
//     called immediately before each plugin's init(), and clearCrashMarker() right after it
//     returns (success or failure). If the process dies mid-init, the marker survives to the next
//     launch; loadEnabledPlugins() sees it, refuses to load that plugin again automatically, and
//     reports it via lastDisabledForSafety() so the UI can tell the user which plugin got
//     auto-disabled and why. The user can manually re-enable it.
//   * Call ProjectBackupManager::snapshot() before loadEnabledPlugins() runs, not after — see
//     ProjectBackupManager.h. This class does not take backups itself; ownership of *when* to
//     snapshot belongs to whoever is orchestrating startup (main.cpp).
class NativePluginManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList plugins READ pluginsForQml NOTIFY pluginsChanged)

public:
    explicit NativePluginManager(QObject *parent = nullptr);
    ~NativePluginManager() override;

    // Absolute dirs, created on first use.
    static QString pluginsRootDir();               // <AppDataLocation>/plugins/native
    static QString pluginDataDir(const QString &id);

    // Scans pluginsRootDir() for one subfolder per plugin id, resolves the platform-appropriate
    // library inside it, and loads any that are not marked disabled in plugins/native/state.json
    // and not sitting under an unresolved crash marker. Safe to call more than once; already-
    // loaded plugins are left alone, removed folders are unloaded.
    Q_INVOKABLE void loadEnabledPlugins(AppController *appController, QQmlApplicationEngine *engine);

    Q_INVOKABLE void unloadAll();

    // Per-plugin enable/disable, persisted to plugins/native/state.json. Disabling an already-
    // loaded plugin calls shutdown() and unloads it immediately; it will not be picked up by the
    // next loadEnabledPlugins() call until re-enabled.
    Q_INVOKABLE bool setPluginEnabled(const QString &id, bool enabled);
    Q_INVOKABLE bool isPluginEnabled(const QString &id) const;

    // The id most recently auto-disabled for crashing during init, or empty. Cleared after being
    // read once so the UI shows the warning a single time per occurrence.
    Q_INVOKABLE QString takeLastDisabledForSafety();

    QVariantList pluginsForQml() const;

signals:
    void pluginsChanged();
    void pluginLoadFailed(const QString &id, const QString &error);
    void pluginLoaded(const QString &id);

private:
    struct LoadedPlugin
    {
        QString id;
        QString libraryPath;
        QPluginLoader *loader = nullptr;
        drift::plugin::IDriftPlugin *instance = nullptr;
        QString displayName;
        QString version;
        QString author;
        QString error; // non-empty if load/init failed
    };

    void writeCrashMarker(const QString &id);
    void clearCrashMarker(const QString &id);
    bool hasUnresolvedCrashMarker(const QString &id) const;

    QString stateFilePath() const;
    QJsonObject readState() const;
    bool writeState(const QJsonObject &state) const;

    QList<LoadedPlugin> m_plugins;
    QString m_lastDisabledForSafety;
};
