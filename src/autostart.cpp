#include "autostart.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>

#include <algorithm>

namespace {
AutostartEntry parseEntry(const QString &id, const QString &path, bool userEntry) {
    AutostartEntry entry{id, id, path, true, userEntry};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return entry;
    bool inDesktopEntry = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
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
