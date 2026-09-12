#include "ProjectBackupManager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVariantMap>

ProjectBackupManager::ProjectBackupManager(QObject *parent)
    : QObject(parent)
{
}

QString ProjectBackupManager::backupsRootDir()
{
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/backups");
    QDir().mkpath(base);
    return base;
}

QString ProjectBackupManager::projectId(const QString &projectPath)
{
    // Short, filesystem-safe, stable per absolute path. Collisions are harmless (worst case two
    // unrelated projects share a backup folder); a full hash isn't worth the unreadable folder
    // name, so this trades a little of Qt's fast hash for something a user could still eyeball.
    const QByteArray hash =
        QCryptographicHash::hash(QFileInfo(projectPath).absoluteFilePath().toUtf8(),
                                  QCryptographicHash::Md5)
            .toHex()
            .left(10);
    const QString baseName = QFileInfo(projectPath).completeBaseName();
    const QString safeName = QString(baseName).replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")),
                                                         QStringLiteral("_"));
    return safeName.isEmpty() ? QString::fromUtf8(hash)
                               : safeName + QLatin1Char('-') + QString::fromUtf8(hash);
}

void ProjectBackupManager::setMaxBackupsPerProject(int max)
{
    if (max < 1)
        max = 1;
    if (max == m_maxBackupsPerProject)
        return;
    m_maxBackupsPerProject = max;
    emit maxBackupsPerProjectChanged();
}

QString ProjectBackupManager::snapshot(const QString &projectJsonPath, const QString &reason,
                                        QString *error)
{
    QFileInfo src(projectJsonPath);
    if (!src.exists() || !src.isFile()) {
        if (error)
            *error = QStringLiteral("Project file does not exist: %1").arg(projectJsonPath);
        return {};
    }

    const QString dir = backupsRootDir() + QLatin1Char('/') + projectId(projectJsonPath);
    QDir().mkpath(dir);

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString destPath = dir + QLatin1Char('/') + stamp + QStringLiteral(".dcut.json");

    // Same-second collision (two snapshots requested back to back): suffix with a counter rather
    // than silently overwriting the previous one.
    QString finalPath = destPath;
    int suffix = 1;
    while (QFile::exists(finalPath)) {
        finalPath = dir + QLatin1Char('/') + stamp + QStringLiteral("-%1.dcut.json").arg(suffix++);
    }

    if (!QFile::copy(projectJsonPath, finalPath)) {
        if (error)
            *error = QStringLiteral("Could not copy project file to backup location");
        return {};
    }

    QJsonObject meta;
    meta[QStringLiteral("reason")] = reason;
    meta[QStringLiteral("sourcePath")] = src.absoluteFilePath();
    meta[QStringLiteral("timestamp")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    QFile metaFile(finalPath + QStringLiteral(".meta.json"));
    if (metaFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        metaFile.write(QJsonDocument(meta).toJson(QJsonDocument::Compact));

    pruneOldBackups(dir);
    return finalPath;
}

void ProjectBackupManager::pruneOldBackups(const QString &projectDir)
{
    QDir dir(projectDir);
    QFileInfoList entries = dir.entryInfoList({QStringLiteral("*.dcut.json")}, QDir::Files,
                                               QDir::Time /* newest first */);
    for (int i = m_maxBackupsPerProject; i < entries.size(); ++i) {
        QFile::remove(entries[i].absoluteFilePath());
        QFile::remove(entries[i].absoluteFilePath() + QStringLiteral(".meta.json"));
    }
}

QVariantList ProjectBackupManager::listBackups(const QString &projectJsonPath) const
{
    QVariantList result;
    const QString dir = backupsRootDir() + QLatin1Char('/') + projectId(projectJsonPath);
    QFileInfoList entries = QDir(dir).entryInfoList({QStringLiteral("*.dcut.json")}, QDir::Files,
                                                     QDir::Time /* newest first */);
    for (const QFileInfo &fi : entries) {
        QString reason;
        QFile metaFile(fi.absoluteFilePath() + QStringLiteral(".meta.json"));
        if (metaFile.open(QIODevice::ReadOnly)) {
            const auto obj = QJsonDocument::fromJson(metaFile.readAll()).object();
            reason = obj.value(QStringLiteral("reason")).toString();
        }
        QVariantMap m;
        m[QStringLiteral("path")] = fi.absoluteFilePath();
        m[QStringLiteral("timestamp")] = fi.lastModified().toString(Qt::ISODate);
        m[QStringLiteral("reason")] = reason;
        m[QStringLiteral("sizeBytes")] = static_cast<qint64>(fi.size());
        result.append(m);
    }
    return result;
}

bool ProjectBackupManager::restoreBackup(const QString &backupPath, const QString &projectJsonPath,
                                          QString *error)
{
    if (!QFile::exists(backupPath)) {
        if (error)
            *error = QStringLiteral("Backup file not found: %1").arg(backupPath);
        return false;
    }

    // Restoring is itself a mutation of the live project file — snapshot what's there first so
    // "restore" can't be a one-way trip if the wrong backup gets picked.
    QString snapshotError;
    if (QFile::exists(projectJsonPath))
        snapshot(projectJsonPath, QStringLiteral("before restore"), &snapshotError);

    if (QFile::exists(projectJsonPath) && !QFile::remove(projectJsonPath)) {
        if (error)
            *error = QStringLiteral("Could not replace existing project file");
        return false;
    }
    if (!QFile::copy(backupPath, projectJsonPath)) {
        if (error)
            *error = QStringLiteral("Could not copy backup over project file");
        return false;
    }
    return true;
}
