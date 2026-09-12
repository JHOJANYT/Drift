#include "NativePluginManager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QStandardPaths>
#include <QVariantMap>

namespace {

// Mirrors drift::addon::currentPlatform()'s "<os>-<arch>" shape, but only the OS half matters
// here — QLibrary already handles the extension per-platform, this just fixes the *filename stem*
// each plugin folder must provide.
QString platformLibStem()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("plugin-win");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("plugin-macos");
#else
    return QStringLiteral("plugin-linux");
#endif
}

void hostLog(const QString &pluginId, const QString &message)
{
    qInfo().noquote() << QStringLiteral("[plugin:%1] %2").arg(pluginId, message);
}

} // namespace

NativePluginManager::NativePluginManager(QObject *parent)
    : QObject(parent)
{
}

NativePluginManager::~NativePluginManager()
{
    unloadAll();
}

QString NativePluginManager::pluginsRootDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                          + QStringLiteral("/plugins/native");
    QDir().mkpath(base);
    return base;
}

QString NativePluginManager::pluginDataDir(const QString &id)
{
    const QString dir = pluginsRootDir() + QLatin1Char('/') + id + QStringLiteral("/data");
    QDir().mkpath(dir);
    return dir;
}

QString NativePluginManager::stateFilePath() const
{
    return pluginsRootDir() + QStringLiteral("/state.json");
}

QJsonObject NativePluginManager::readState() const
{
    QFile file(stateFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const auto doc = QJsonDocument::fromJson(file.readAll());
    return doc.object();
}

bool NativePluginManager::writeState(const QJsonObject &state) const
{
    QFile file(stateFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(state).toJson(QJsonDocument::Indented));
    return true;
}

bool NativePluginManager::isPluginEnabled(const QString &id) const
{
    const QJsonObject state = readState();
    // Default is enabled: a plugin folder the user placed there themselves is opt-in by the act
    // of putting it in plugins/native/ in the first place. Explicit "false" is what disables it.
    return state.value(id).toObject().value(QStringLiteral("enabled")).toBool(true);
}

bool NativePluginManager::setPluginEnabled(const QString &id, bool enabled)
{
    QJsonObject state = readState();
    QJsonObject entry = state.value(id).toObject();
    entry[QStringLiteral("enabled")] = enabled;
    state[id] = entry;
    if (!writeState(state))
        return false;

    if (!enabled) {
        for (int i = 0; i < m_plugins.size(); ++i) {
            if (m_plugins[i].id != id)
                continue;
            if (m_plugins[i].instance)
                m_plugins[i].instance->shutdown();
            delete m_plugins[i].loader; // unloads the library
            m_plugins.removeAt(i);
            emit pluginsChanged();
            break;
        }
    }
    return true;
}

QString NativePluginManager::takeLastDisabledForSafety()
{
    const QString id = m_lastDisabledForSafety;
    m_lastDisabledForSafety.clear();
    return id;
}

// --- crash marker -----------------------------------------------------------------------------
// A zero-byte sentinel file per plugin id, written immediately before init() and removed right
// after. Its mere presence at the next launch means init() never returned last time — most likely
// a hard crash (signal), since a normal exception/return path always reaches clearCrashMarker().

QString crashMarkerPath(const QString &id)
{
    return NativePluginManager::pluginsRootDir() + QLatin1Char('/') + id
        + QStringLiteral(".loading");
}

void NativePluginManager::writeCrashMarker(const QString &id)
{
    QFile marker(crashMarkerPath(id));
    marker.open(QIODevice::WriteOnly | QIODevice::Truncate);
}

void NativePluginManager::clearCrashMarker(const QString &id)
{
    QFile::remove(crashMarkerPath(id));
}

bool NativePluginManager::hasUnresolvedCrashMarker(const QString &id) const
{
    return QFile::exists(crashMarkerPath(id));
}

// --- loading ------------------------------------------------------------------------------------

void NativePluginManager::loadEnabledPlugins(AppController *appController, QQmlApplicationEngine *engine)
{
    const QDir root(pluginsRootDir());
    const QStringList ids = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &id : ids) {
        // Already loaded this session — leave it alone.
        bool alreadyLoaded = false;
        for (const auto &p : std::as_const(m_plugins)) {
            if (p.id == id) { alreadyLoaded = true; break; }
        }
        if (alreadyLoaded)
            continue;

        if (!isPluginEnabled(id)) {
            qInfo() << "Plugin" << id << "is disabled, skipping.";
            continue;
        }

        if (hasUnresolvedCrashMarker(id)) {
            qWarning() << "Plugin" << id
                       << "left an unresolved crash marker from a previous launch — "
                          "auto-disabling instead of loading it again.";
            setPluginEnabled(id, false);
            m_lastDisabledForSafety = id;
            emit pluginLoadFailed(id, QStringLiteral("Crashed during init on a previous launch"));
            continue;
        }

        const QString libPath = root.filePath(id + QLatin1Char('/') + platformLibStem());
        if (!QLibrary::isLibrary(libPath) && !QFile::exists(libPath)) {
            // QLibrary::isLibrary() needs a real extension to judge; try common ones explicitly
            // so plugin authors don't have to guess Qt's exact suffix logic per platform.
            bool found = false;
            for (const QString &suffix : {QStringLiteral(".so"), QStringLiteral(".dll"),
                                           QStringLiteral(".dylib")}) {
                if (QFile::exists(libPath + suffix)) { found = true; break; }
            }
            if (!found) {
                qWarning() << "Plugin folder" << id << "has no" << platformLibStem()
                           << "(.so/.dll/.dylib) for this platform — skipping.";
                continue;
            }
        }

        auto *loader = new QPluginLoader(libPath, this);
        QObject *rawInstance = loader->instance(); // triggers load()
        if (!rawInstance) {
            const QString err = loader->errorString();
            qWarning() << "Failed to load plugin" << id << ":" << err;
            emit pluginLoadFailed(id, err);
            delete loader;
            continue;
        }

        auto *plugin = qobject_cast<drift::plugin::IDriftPlugin *>(rawInstance);
        if (!plugin) {
            const QString err = QStringLiteral("Library does not implement IDriftPlugin");
            qWarning() << "Plugin" << id << ":" << err;
            emit pluginLoadFailed(id, err);
            loader->unload();
            delete loader;
            continue;
        }

        if (plugin->abiVersion() != kDriftPluginAbiVersion) {
            const QString err = QStringLiteral("ABI version mismatch (plugin: %1, host: %2) — "
                                                "rebuild the plugin against this Drift version")
                                     .arg(plugin->abiVersion())
                                     .arg(kDriftPluginAbiVersion);
            qWarning() << "Plugin" << id << ":" << err;
            emit pluginLoadFailed(id, err);
            loader->unload();
            delete loader;
            continue;
        }

        drift::plugin::DriftHostContext ctx;
        ctx.abiVersion = kDriftPluginAbiVersion;
        ctx.appController = appController;
        ctx.qmlEngine = engine;
        ctx.pluginDataDir = pluginDataDir(id);
        ctx.log = &hostLog;

        QString initError;
        writeCrashMarker(id);
        const bool ok = plugin->init(ctx, &initError);
        clearCrashMarker(id); // reached only if init() didn't crash the process

        LoadedPlugin entry;
        entry.id = id;
        entry.libraryPath = libPath;
        entry.loader = loader;
        entry.displayName = plugin->displayName();
        entry.version = plugin->version();
        entry.author = plugin->author();

        if (!ok) {
            entry.error = initError.isEmpty() ? QStringLiteral("init() returned false") : initError;
            qWarning() << "Plugin" << id << "failed to initialize:" << entry.error;
            emit pluginLoadFailed(id, entry.error);
            loader->unload();
            delete loader;
            continue;
        }

        entry.instance = plugin;
        m_plugins.append(entry);
        qInfo() << "Loaded plugin" << id << entry.displayName << entry.version;
        emit pluginLoaded(id);
    }

    emit pluginsChanged();
}

void NativePluginManager::unloadAll()
{
    for (auto &p : m_plugins) {
        if (p.instance)
            p.instance->shutdown();
        if (p.loader) {
            p.loader->unload();
            delete p.loader;
        }
    }
    m_plugins.clear();
}

QVariantList NativePluginManager::pluginsForQml() const
{
    QVariantList list;
    for (const auto &p : m_plugins) {
        QVariantMap m;
        m[QStringLiteral("id")] = p.id;
        m[QStringLiteral("displayName")] = p.displayName;
        m[QStringLiteral("version")] = p.version;
        m[QStringLiteral("author")] = p.author;
        m[QStringLiteral("error")] = p.error;
        m[QStringLiteral("enabled")] = isPluginEnabled(p.id);
        list.append(m);
    }
    return list;
}
