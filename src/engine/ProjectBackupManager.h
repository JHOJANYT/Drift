#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

// Timestamped, rotating backups of the *project JSON* (not the media — media stays where it is;
// this is cheap because a .dcut.json is small). Separate from the existing autosave/recovery
// snapshot in AppController (which covers "app crashed, restore what I was doing"): backups here
// are multiple, dated, and kept even across clean exits, specifically so a bad plugin or a bad
// edit can be rolled back from further than "the last autosave".
//
// Layout: <AppDataLocation>/backups/<projectId>/<yyyyMMdd-HHmmss>.dcut.json
// projectId is derived from the project file's path (stable across saves of the same file, so a
// project doesn't share a backup folder with an unrelated one that happens to autosave at the
// same second).
class ProjectBackupManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int maxBackupsPerProject READ maxBackupsPerProject WRITE setMaxBackupsPerProject
                   NOTIFY maxBackupsPerProjectChanged)

public:
    explicit ProjectBackupManager(QObject *parent = nullptr);

    static QString backupsRootDir(); // <AppDataLocation>/backups

    // Stable id for a project path — same input always maps to the same folder name, so repeated
    // snapshots of one project accumulate together instead of scattering.
    static QString projectId(const QString &projectPath);

    int maxBackupsPerProject() const { return m_maxBackupsPerProject; }
    void setMaxBackupsPerProject(int max);

    // Copies projectJsonPath into that project's backup folder under a timestamped name, then
    // deletes the oldest entries beyond maxBackupsPerProject. `reason` is stored alongside the
    // copy (in a sibling .meta.json) purely for the UI — "before plugin load", "manual", "autosave
    // promoted" — it does not affect rotation or restore.
    //
    // Returns the path of the new backup file, or empty on failure (see *error).
    Q_INVOKABLE QString snapshot(const QString &projectJsonPath, const QString &reason,
                                  QString *error = nullptr);

    // Newest first. Each entry: {path, timestamp (ISO 8601), reason, sizeBytes}.
    Q_INVOKABLE QVariantList listBackups(const QString &projectJsonPath) const;

    // Copies a backup file back over projectJsonPath. Takes a fresh "before restore" snapshot
    // first so restoring is itself undoable. Caller is responsible for reloading the project in
    // the running app afterward — this only touches the file on disk.
    Q_INVOKABLE bool restoreBackup(const QString &backupPath, const QString &projectJsonPath,
                                    QString *error = nullptr);

signals:
    void maxBackupsPerProjectChanged();

private:
    void pruneOldBackups(const QString &projectDir);

    int m_maxBackupsPerProject = 15;
};
