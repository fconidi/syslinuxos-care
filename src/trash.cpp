#include "trash.h"

#include "scanner.h"

#include <QDir>
#include <QFile>
#include <QSet>
#include <QUrl>

#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
class Fd {
public:
    explicit Fd(int value = -1) : value_(value) {}
    ~Fd() { if (value_ >= 0) ::close(value_); }
    int get() const { return value_; }
private:
    int value_;
};

QString osError() { return QString::fromLocal8Bit(std::strerror(errno)); }

bool safeName(const QByteArray &name) {
    return !name.isEmpty() && name != "." && name != ".." && !name.contains('/');
}

bool removeTree(int parent, const QByteArray &name, dev_t device, QString &error,
                ino_t expectedInode = 0) {
    struct stat info {};
    if (::fstatat(parent, name.constData(), &info, AT_SYMLINK_NOFOLLOW) != 0) {
        error = osError();
        return false;
    }
    if (info.st_dev != device) {
        error = "Trash item crosses a file system boundary";
        return false;
    }
    if (expectedInode != 0 && info.st_ino != expectedInode) {
        error = "Trash item changed during removal";
        return false;
    }
    if (!S_ISDIR(info.st_mode)) {
        if (!S_ISREG(info.st_mode) && !S_ISLNK(info.st_mode)) {
            error = "Unsupported trash item type";
            return false;
        }
        if (::unlinkat(parent, name.constData(), 0) != 0) {
            error = osError();
            return false;
        }
        return true;
    }
    Fd child(::openat(parent, name.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (child.get() < 0) { error = osError(); return false; }
    struct stat openedInfo {};
    if (::fstat(child.get(), &openedInfo) != 0 || openedInfo.st_dev != info.st_dev ||
        openedInfo.st_ino != info.st_ino) {
        error = "Trash directory changed during removal";
        return false;
    }
    const int streamFd = ::dup(child.get());
    if (streamFd < 0) { error = osError(); return false; }
    DIR *stream = ::fdopendir(streamFd);
    if (!stream) { ::close(streamFd); error = osError(); return false; }
    bool success = true;
    while (dirent *item = ::readdir(stream)) {
        const QByteArray part(item->d_name);
        if (part == "." || part == "..") continue;
        if (!safeName(part) || !removeTree(child.get(), part, device, error)) {
            success = false;
            break;
        }
    }
    ::closedir(stream);
    if (!success) return false;
    if (::unlinkat(parent, name.constData(), AT_REMOVEDIR) != 0) {
        error = osError();
        return false;
    }
    return true;
}
}

namespace {
TrashScan scanTrashRoot(const QString &trashRoot, const QString &originalBase) {
    TrashScan result;
    result.root = trashRoot;
    Fd root(::open(QFile::encodeName(result.root).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (root.get() < 0) {
        if (errno != ENOENT) result.error = osError();
        return result;
    }
    struct stat rootInfo {};
    if (::fstat(root.get(), &rootInfo) != 0) { result.error = osError(); return result; }
    result.rootDevice = rootInfo.st_dev;
    result.rootInode = rootInfo.st_ino;
    Fd files(::openat(root.get(), "files", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    Fd info(::openat(root.get(), "info", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (files.get() < 0 || info.get() < 0) { result.error = osError(); return result; }
    QDir infoDir(QDir(result.root).filePath("info"));
    for (const QString &filename : infoDir.entryList({"*.trashinfo"}, QDir::Files | QDir::NoSymLinks)) {
        const QString id = filename.left(filename.size() - 10);
        const QByteArray name = QFile::encodeName(id);
        if (!safeName(name)) continue;
        struct stat payload {}, metadata {};
        if (::fstatat(files.get(), name.constData(), &payload, AT_SYMLINK_NOFOLLOW) != 0 ||
            ::fstatat(info.get(), QFile::encodeName(filename).constData(), &metadata, AT_SYMLINK_NOFOLLOW) != 0 ||
            !S_ISREG(metadata.st_mode)) continue;
        QFile infoFile(infoDir.filePath(filename));
        if (!infoFile.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QString originalPath;
        QString deletionDate;
        bool inTrashInfo = false;
        while (!infoFile.atEnd()) {
            const QByteArray line = infoFile.readLine().trimmed();
            if (line.startsWith('[')) { inTrashInfo = line == "[Trash Info]"; continue; }
            if (!inTrashInfo) continue;
            if (line.startsWith("Path=") && originalPath.isEmpty()) {
                const QString decoded = QUrl::fromPercentEncoding(line.mid(5));
                if (QDir::isAbsolutePath(decoded)) originalPath = decoded;
                else if (!decoded.split('/').contains(".."))
                    originalPath = QDir(originalBase).filePath(decoded);
            }
            if (line.startsWith("DeletionDate=")) deletionDate = QString::fromUtf8(line.mid(13));
        }
        quint64 size = S_ISREG(payload.st_mode) ? quint64(payload.st_size) : 0;
        if (S_ISDIR(payload.st_mode)) {
            const auto nested = scanRegularFiles(QDir(QDir(result.root).filePath("files")).filePath(id));
            for (const auto &item : nested.items) size += item.size;
        }
        result.entries.append({id, originalPath, deletionDate, size,
                               quint64(payload.st_dev), quint64(payload.st_ino),
                               quint64(metadata.st_ino), payload.st_mtim.tv_sec,
                               payload.st_mtim.tv_nsec, metadata.st_mtim.tv_sec,
                               metadata.st_mtim.tv_nsec, bool(S_ISDIR(payload.st_mode))});
    }
    return result;
}

void inspectMountTrash(int parentFd, const QByteArray &name, uid_t userId,
                       dev_t expectedDevice, const QString &trashRoot,
                       const QString &originalBase, QVector<TrashScan> &scans) {
    Fd directory(::openat(parentFd, name.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (directory.get() < 0) return;
    struct stat info {};
    if (::fstat(directory.get(), &info) != 0 || info.st_uid != userId ||
        info.st_dev != expectedDevice) return;
    const TrashScan scan = scanTrashRoot(trashRoot, originalBase);
    if (scan.error.isEmpty() && scan.rootDevice == quint64(info.st_dev) &&
        scan.rootInode == quint64(info.st_ino)) scans.append(scan);
}
}

TrashScan scanHomeTrash(const QString &dataHome) {
    return scanTrashRoot(QDir(dataHome).filePath("Trash"), dataHome);
}

QVector<TrashScan> scanTrashLocations(const QString &dataHome, const QStringList &mountRoots,
                                     quint32 userId) {
    QVector<TrashScan> scans{scanHomeTrash(dataHome)};
    QSet<QString> seen;
    seen.insert(QDir(dataHome).filePath("Trash"));
    const QByteArray uid = QByteArray::number(userId);
    for (const QString &mountRoot : mountRoots) {
        const QString rootPath = QDir::cleanPath(mountRoot);
        if (!QDir::isAbsolutePath(rootPath)) continue;
        Fd mount(::open(QFile::encodeName(rootPath).constData(),
                        O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (mount.get() < 0) continue;
        struct stat mountInfo {};
        if (::fstat(mount.get(), &mountInfo) != 0) continue;
        Fd shared(::openat(mount.get(), ".Trash", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (shared.get() >= 0) {
            struct stat info {};
            if (::fstat(shared.get(), &info) == 0 && info.st_dev == mountInfo.st_dev &&
                (info.st_mode & S_ISVTX)) {
                const QString path = QDir(rootPath).filePath(".Trash/" + QString::number(userId));
                if (!seen.contains(path)) {
                    inspectMountTrash(shared.get(), uid, userId, mountInfo.st_dev, path,
                                      rootPath, scans);
                    seen.insert(path);
                }
            }
        }
        const QByteArray privateName = ".Trash-" + uid;
        const QString privatePath = QDir(rootPath).filePath(QString::fromLocal8Bit(privateName));
        if (!seen.contains(privatePath)) {
            inspectMountTrash(mount.get(), privateName, userId, mountInfo.st_dev,
                              privatePath, rootPath, scans);
            seen.insert(privatePath);
        }
    }
    return scans;
}

bool removeTrashEntry(const TrashScan &scan, const TrashEntry &entry, QString &error) {
    error.clear();
    if (scan.root.isEmpty() || !safeName(QFile::encodeName(entry.id))) {
        error = "Invalid trash entry";
        return false;
    }
    Fd root(::open(QFile::encodeName(scan.root).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (root.get() < 0) { error = osError(); return false; }
    struct stat rootInfo {};
    if (::fstat(root.get(), &rootInfo) != 0 || quint64(rootInfo.st_dev) != scan.rootDevice ||
        quint64(rootInfo.st_ino) != scan.rootInode) {
        error = "Trash directory changed after scan";
        return false;
    }
    Fd files(::openat(root.get(), "files", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    Fd info(::openat(root.get(), "info", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (files.get() < 0 || info.get() < 0) { error = osError(); return false; }
    const QByteArray name = QFile::encodeName(entry.id);
    const QByteArray infoName = name + ".trashinfo";
    struct stat payload {}, metadata {};
    if (::fstatat(files.get(), name.constData(), &payload, AT_SYMLINK_NOFOLLOW) != 0 ||
        ::fstatat(info.get(), infoName.constData(), &metadata, AT_SYMLINK_NOFOLLOW) != 0 ||
        quint64(payload.st_dev) != entry.device || quint64(payload.st_ino) != entry.inode ||
        quint64(metadata.st_ino) != entry.infoInode || !S_ISREG(metadata.st_mode) ||
        payload.st_mtim.tv_sec != entry.modifiedSeconds ||
        payload.st_mtim.tv_nsec != entry.modifiedNanoseconds ||
        (!entry.directory && S_ISREG(payload.st_mode) && quint64(payload.st_size) != entry.size) ||
        metadata.st_mtim.tv_sec != entry.infoModifiedSeconds ||
        metadata.st_mtim.tv_nsec != entry.infoModifiedNanoseconds) {
        error = "Trash entry changed after scan";
        return false;
    }
    if (!removeTree(files.get(), name, payload.st_dev, error, payload.st_ino)) return false;
    if (::unlinkat(info.get(), infoName.constData(), 0) != 0) {
        error = osError();
        return false;
    }
    return true;
}
