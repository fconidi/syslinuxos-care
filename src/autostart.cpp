#include "autostart.h"

#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSaveFile>

#include <algorithm>

namespace {
constexpr auto managedMarker = "X-SysLinuxOS-Care-Override=true";

QByteArray managedOverride(const QString &name) {
    return "[Desktop Entry]\nType=Application\nName=" + name.toUtf8()
           + "\nHidden=true\n" + managedMarker + "\n";
}

QByteArray updateFlag(const QByteArray &source, const QByteArray &key, const QByteArray &value) {
    QList<QByteArray> lines = source.split('\n');
    bool inEntry = false;
    bool found = false;
    for (QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.startsWith('[')) {
            if (inEntry && !found) line = key + '=' + value + '\n' + line;
            inEntry = trimmed == "[Desktop Entry]";
        } else if (inEntry && trimmed.startsWith(key + '=')) {
            line = key + '=' + value;
            found = true;
        }
    }
    QByteArray result = lines.join('\n');
    if (!found && inEntry) {
        if (!result.endsWith('\n')) result += '\n';
        result += key + '=' + value + '\n';
    }
    return result;
}

bool writeFile(const QString &path, const QByteArray &content, QString &error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size() || !file.commit()) {
        error = file.errorString();
        return false;
    }
    return true;
}

AutostartEntry parseEntry(const QString &id, const QString &path, bool userEntry) {
    AutostartEntry entry{id, id, path, true, userEntry};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return entry;
    const QByteArray content = file.readAll();
    entry.fingerprint = QCryptographicHash::hash(content, QCryptographicHash::Sha256);
    bool inDesktopEntry = false;
    for (const QByteArray &rawLine : content.split('\n')) {
        const QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.startsWith('[')) {
            inDesktopEntry = line == "[Desktop Entry]";
            continue;
        }
        if (!inDesktopEntry || line.startsWith('#')) continue;
        const int separator = line.indexOf('=');
        if (separator < 0) continue;
        const QString key = line.left(separator).trimmed();
        const QString value = line.mid(separator + 1).trimmed();
        if (key == "Name" && !value.isEmpty()) entry.name = value;
        if (key == "Hidden" && value.compare("true", Qt::CaseInsensitive) == 0) entry.enabled = false;
        if (key == "X-GNOME-Autostart-enabled" && value.compare("false", Qt::CaseInsensitive) == 0)
            entry.enabled = false;
    }
    return entry;
}
}

bool setAutostartEnabled(const AutostartEntry &entry, bool enabled,
                         const QString &userConfigHome, const QStringList &systemConfigDirs,
                         QString &error) {
    error.clear();
    if (entry.id != QFileInfo(entry.id).fileName() || !entry.id.endsWith(".desktop")) {
        error = "Invalid desktop file name";
        return false;
    }
    const auto current = listAutostart(userConfigHome, systemConfigDirs);
    const auto it = std::find_if(current.cbegin(), current.cend(), [&](const auto &item) {
        return item.id == entry.id;
    });
    if (it == current.cend() || it->path != entry.path || it->enabled != entry.enabled ||
        entry.fingerprint.isEmpty() || it->fingerprint != entry.fingerprint) {
        error = "Startup entry changed; refresh the list";
        return false;
    }
    if (entry.enabled == enabled) return true;
    QDir userDir(QDir(userConfigHome).filePath("autostart"));
    if (QFileInfo(userDir.path()).isSymLink()) {
        error = "Startup directory is a symbolic link";
        return false;
    }
    if (!userDir.exists() && !QDir().mkpath(userDir.path())) {
        error = "Cannot create user startup directory";
        return false;
    }
    const QString userPath = userDir.filePath(entry.id);
    if (QFileInfo(userPath).isSymLink()) {
        error = "Startup entry is a symbolic link";
        return false;
    }
    if (!entry.userEntry) {
        if (enabled || QFileInfo::exists(userPath)) {
            error = "System entry cannot be overridden safely";
            return false;
        }
        return writeFile(userPath, managedOverride(entry.name), error);
    }
    QFile file(userPath);
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return false;
    }
    const QByteArray original = file.readAll();
    file.close();
    if (original == managedOverride(entry.name)) {
        if (!enabled) return true;
        if (!QFile::remove(userPath)) {
            error = "Cannot remove managed startup override";
            return false;
        }
        return true;
    }
    if (!original.contains("[Desktop Entry]")) {
        error = "Invalid desktop entry";
        return false;
    }
    QByteArray updated = updateFlag(original, "Hidden", enabled ? "false" : "true");
    if (enabled && original.contains("X-GNOME-Autostart-enabled=false"))
        updated = updateFlag(updated, "X-GNOME-Autostart-enabled", "true");
    return writeFile(userPath, updated, error);
}

QVector<AutostartEntry> listAutostart(const QString &userConfigHome,
                                     const QStringList &systemConfigDirs) {
    QMap<QString, AutostartEntry> chosen;
    QStringList dirs = systemConfigDirs;
    std::reverse(dirs.begin(), dirs.end());
    for (const QString &base : dirs + QStringList{userConfigHome}) {
        QDir directory(QDir(base).filePath("autostart"));
        if (!directory.exists()) continue;
        for (const QString &id : directory.entryList({"*.desktop"}, QDir::Files | QDir::NoSymLinks,
                                                       QDir::Name)) {
            const QString path = directory.filePath(id);
            if (QFileInfo(path).isSymLink()) continue;
            chosen[id] = parseEntry(id, path, base == userConfigHome);
        }
    }
    return chosen.values().toVector();
}
