#include "browsercache.h"

#include <QDir>
#include <QCryptographicHash>
#include <QFileInfo>

#include <dirent.h>
#include <fcntl.h>
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

namespace {
bool safeDirectory(const QString &path) {
    const QFileInfo info(path);
    return info.isDir() && !info.isSymLink() &&
           info.canonicalFilePath() == QDir::cleanPath(info.absoluteFilePath());
}

QStringList databasePaths(const QString &primary) {
    return {primary, primary + "-wal", primary + "-shm", primary + "-journal"};
}

void addData(QVector<BrowserData> &result, BrowserKind browser, BrowserDataKind kind,
             const QString &profile, const QString &root, const QString &primary) {
    const QFileInfo file(QDir(root).filePath(primary));
    if (file.isFile() && !file.isSymLink())
        result.append({browser, kind, profile, root, databasePaths(primary)});
}
}

QVector<BrowserData> discoverBrowserData(const QString &configHome, const QString &homePath) {
    QVector<BrowserData> result;
    const QString firefoxRoot = QDir(homePath).filePath(".mozilla/firefox");
    if (safeDirectory(firefoxRoot)) {
        const QDir profiles(firefoxRoot);
        for (const QString &profile : profiles.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
            const QString root = profiles.filePath(profile);
            if (safeDirectory(root))
                addData(result, BrowserKind::Firefox, BrowserDataKind::Cookies,
                        profile, root, "cookies.sqlite");
        }
    }
    const struct { BrowserKind kind; const char *name; } kinds[] = {
        {BrowserKind::Chromium, "chromium"},
        {BrowserKind::Chrome, "google-chrome"},
    };
    for (const auto &kind : kinds) {
        const QString browserRoot = QDir(configHome).filePath(kind.name);
        if (!safeDirectory(browserRoot)) continue;
        const QDir profiles(browserRoot);
        for (const QString &profile : profiles.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
            const QString root = profiles.filePath(profile);
            if (!safeDirectory(root)) continue;
            addData(result, kind.kind, BrowserDataKind::History, profile, root, "History");
            const QString cookiePath = safeDirectory(QDir(root).filePath("Network")) &&
                                       QFileInfo(QDir(root).filePath("Network/Cookies")).isFile()
                ? "Network/Cookies" : "Cookies";
            addData(result, kind.kind, BrowserDataKind::Cookies, profile, root, cookiePath);
        }
    }
    return result;
}

ScanResult scanBrowserData(const BrowserData &data, const std::atomic_bool *cancel) {
    if (!safeDirectory(data.root)) {
        ScanResult scan;
        scan.error = QStringLiteral("Browser profile path is unavailable or linked");
        return scan;
    }
    ScanResult scan = scanNamedFiles(data.root, data.paths, cancel);
    if (scan.cancelled) return scan;
    if (!scan.error.isEmpty()) return scan;
    if (scan.items.isEmpty() || scan.items.first().relativePath != data.paths.first())
        scan.error = QStringLiteral("Browser database is unavailable or unsupported");
    for (const auto &path : data.paths) {
        const QFileInfo info(QDir(data.root).filePath(path));
        if (!info.exists() && !info.isSymLink()) continue;
        bool scanned = false;
        for (const auto &item : scan.items)
            if (item.relativePath == path) scanned = true;
        if (!scanned) scan.error = QStringLiteral("Browser database contains an unsupported file");
    }
    return scan;
}

QString browserDataFingerprint(const ScanResult &scan) {
    if (!scan.error.isEmpty() || scan.root.isEmpty() || scan.items.isEmpty()) return {};
    QByteArray data;
    auto append = [&data](const QByteArray &part) {
        data += QByteArray::number(part.size()) + ':' + part;
    };
    append(scan.root.toUtf8());
    append(QByteArray::number(scan.rootDevice));
    append(QByteArray::number(scan.rootInode));
    append(QByteArray::number(scan.rootMountId));
    append(QByteArray::number(scan.items.size()));
    for (const auto &item : scan.items) {
        append(item.relativePath.toUtf8());
        append(QByteArray::number(item.size));
        append(QByteArray::number(item.device));
        append(QByteArray::number(item.inode));
        append(QByteArray::number(item.modifiedSeconds));
        append(QByteArray::number(item.modifiedNanoseconds));
    }
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

bool browserRunning(BrowserKind kind) {
    // Cleanup calls this before each file. Read full status only for matching names.
    DIR *proc = ::opendir("/proc");
    if (!proc) return false;
    const auto matches = [kind](const QByteArray &name) {
        return (kind == BrowserKind::Firefox && name.startsWith("firefox")) ||
               (kind == BrowserKind::Chromium && name.startsWith("chromium")) ||
               (kind == BrowserKind::Chrome && (name == "chrome" || name.startsWith("google-chrome")));
    };
    bool found = false;
    while (dirent *entry = ::readdir(proc)) {
        const QByteArray id(entry->d_name);
        if (id.isEmpty() || id[0] < '0' || id[0] > '9') continue;
        bool numeric = true;
        for (char digit : id) {
            if (digit < '0' || digit > '9') { numeric = false; break; }
        }
        if (!numeric) continue;
        const QByteArray path = id + "/comm";
        const int fd = ::openat(::dirfd(proc), path.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) continue;
        char buffer[64];
        const ssize_t length = ::read(fd, buffer, sizeof(buffer));
        ::close(fd);
        if (length <= 0) continue;
        const QByteArray name = QByteArray(buffer, length).trimmed().toLower();
        if (!matches(name)) continue;
        const QByteArray statusPath = id + "/status";
        const int statusFd = ::openat(::dirfd(proc), statusPath.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (statusFd < 0) continue;
        char statusBuffer[4096];
        const ssize_t statusLength = ::read(statusFd, statusBuffer, sizeof(statusBuffer));
        ::close(statusFd);
        if (statusLength <= 0) continue;
        bool sameName = false;
        bool owned = false;
        for (const QByteArray &line : QByteArray(statusBuffer, statusLength).split('\n')) {
            if (line.startsWith("Name:")) sameName = matches(line.mid(5).trimmed().toLower());
            if (line.startsWith("Uid:")) {
                bool ok = false;
                const QByteArray uid = line.mid(4).simplified().split(' ').first();
                owned = uid.toUInt(&ok) == ::getuid() && ok;
            }
        }
        found = sameName && owned;
        if (found) break;
    }
    ::closedir(proc);
    return found;
}
