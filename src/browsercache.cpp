#include "browsercache.h"

#include "processes.h"

#include <QDir>
#include <QFileInfo>

#include <unistd.h>

QString browserName(BrowserKind kind) {
    switch (kind) {
    case BrowserKind::Firefox: return "Firefox";
    case BrowserKind::Chromium: return "Chromium";
    case BrowserKind::Chrome: return "Google Chrome";
    }
    return {};
}

QVector<BrowserCache> discoverBrowserCaches(const QString &cacheHome) {
    QVector<BrowserCache> result;
    const struct { BrowserKind kind; const char *root; const char *subdir; } kinds[] = {
        {BrowserKind::Firefox, "mozilla/firefox", "cache2/entries"},
        {BrowserKind::Chromium, "chromium", "Cache/Cache_Data"},
        {BrowserKind::Chrome, "google-chrome", "Cache/Cache_Data"},
    };
    for (const auto &kind : kinds) {
        if (kind.kind == BrowserKind::Firefox &&
            QFileInfo(QDir(cacheHome).filePath("mozilla")).isSymLink()) continue;
        QDir profiles(QDir(cacheHome).filePath(kind.root));
        if (!profiles.exists() || QFileInfo(profiles.path()).isSymLink()) continue;
        for (const QString &profile : profiles.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
            const QDir profileDir(profiles.filePath(profile));
            QString cache = profileDir.filePath(kind.subdir);
            if (kind.kind == BrowserKind::Firefox && QFileInfo(profileDir.filePath("cache2")).isSymLink())
                continue;
            if (kind.kind != BrowserKind::Firefox && QFileInfo(profileDir.filePath("Cache")).isSymLink())
                continue;
            if (kind.kind != BrowserKind::Firefox && !QFileInfo(cache).isDir())
                cache = profileDir.filePath("Cache");
            const QFileInfo info(cache);
            if (info.isDir() && !info.isSymLink()) result.append({kind.kind, profile, cache});
        }
    }
    return result;
}

bool browserRunning(BrowserKind kind) {
    for (const auto &process : listProcesses()) {
        if (process.uid != getuid()) continue;
        const QString name = process.name.toLower();
        if (kind == BrowserKind::Firefox && name.startsWith("firefox")) return true;
        if (kind == BrowserKind::Chromium && name.startsWith("chromium")) return true;
        if (kind == BrowserKind::Chrome && (name == "chrome" || name.startsWith("google-chrome")))
            return true;
    }
    return false;
}
