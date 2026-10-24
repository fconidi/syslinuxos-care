#include "trash.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QRandomGenerator>
#include <QSet>
#include <QUrl>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>

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

bool mountIdAt(int directory, const char *path, int flags, quint64 &mountId) {
    struct statx info {};
    if (::statx(directory, path, flags, STATX_MNT_ID, &info) != 0) return false;
    if (!(info.stx_mask & STATX_MNT_ID)) {
        errno = EOPNOTSUPP;
        return false;
    }
    mountId = info.stx_mnt_id;
    return true;
}

bool sameMount(int directory, const char *path, int flags, quint64 expected) {
    quint64 actual = 0;
    return mountIdAt(directory, path, flags, actual) && actual == expected;
}

bool sameMountFd(int descriptor, quint64 expected) {
    return sameMount(descriptor, "", AT_EMPTY_PATH, expected);
}

TrashNode nodeFromStat(const QByteArray &path, const struct stat &info) {
    return {path, quint64(info.st_dev), quint64(info.st_ino), quint64(info.st_size),
            info.st_mtim.tv_sec, info.st_mtim.tv_nsec, quint32(info.st_mode & S_IFMT)};
}

bool sameNode(const TrashNode &expected, const struct stat &actual) {
    return expected.device == quint64(actual.st_dev) &&
           expected.inode == quint64(actual.st_ino) &&
           expected.type == quint32(actual.st_mode & S_IFMT) &&
           (expected.type == S_IFDIR ||
            (expected.modifiedSeconds == actual.st_mtim.tv_sec &&
             expected.modifiedNanoseconds == actual.st_mtim.tv_nsec &&
             (expected.type != S_IFREG || expected.size == quint64(actual.st_size))));
}

bool captureTree(int directory, const QByteArray &prefix, dev_t device, quint64 mountId,
                 QVector<TrashNode> &nodes, quint64 &bytes, QString &error,
                 const std::atomic_bool *cancel = nullptr) {
    const int streamFd = ::dup(directory);
    if (streamFd < 0) { error = osError(); return false; }
    DIR *stream = ::fdopendir(streamFd);
    if (!stream) { ::close(streamFd); error = osError(); return false; }
    bool complete = true;
    while (dirent *item = ::readdir(stream)) {
        if (cancel && cancel->load()) { complete = false; break; }
        const QByteArray name(item->d_name);
        if (name == "." || name == "..") continue;
        if (!safeName(name) || nodes.size() >= 50000) {
            error = "Trash folder exceeds the safe scan limit";
            complete = false;
            break;
        }
        struct stat info {};
        if (::fstatat(directory, name.constData(), &info, AT_SYMLINK_NOFOLLOW) != 0) {
            error = osError(); complete = false; break;
        }
        if (info.st_dev != device ||
            !sameMount(directory, name.constData(), AT_SYMLINK_NOFOLLOW, mountId) ||
            (!S_ISREG(info.st_mode) && !S_ISLNK(info.st_mode) &&
                                       !S_ISDIR(info.st_mode))) {
            error = "Trash folder contains an unsupported item or mount";
            complete = false; break;
        }
        const QByteArray path = prefix.isEmpty() ? name : prefix + '/' + name;
        nodes.append(nodeFromStat(path, info));
        if (S_ISREG(info.st_mode)) bytes += quint64(info.st_size);
        if (S_ISDIR(info.st_mode)) {
            Fd child(::openat(directory, name.constData(), O_RDONLY | O_DIRECTORY |
                             O_NOFOLLOW | O_CLOEXEC));
            struct stat opened {};
            if (child.get() < 0 || ::fstat(child.get(), &opened) != 0 ||
                opened.st_dev != info.st_dev || opened.st_ino != info.st_ino ||
                !sameMountFd(child.get(), mountId)) {
                error = "Trash folder changed during scan";
                complete = false; break;
            }
            if (!captureTree(child.get(), path, device, mountId, nodes, bytes, error, cancel)) {
                complete = false; break;
            }
        }
    }
    ::closedir(stream);
    return complete;
}

bool sameTree(QVector<TrashNode> expected, QVector<TrashNode> current) {
    if (expected.size() != current.size()) return false;
    auto order = [](const TrashNode &a, const TrashNode &b) { return a.path < b.path; };
    std::sort(expected.begin(), expected.end(), order);
    std::sort(current.begin(), current.end(), order);
    for (int index = 0; index < expected.size(); ++index) {
        const auto &a = expected[index];
        const auto &b = current[index];
        if (a.path != b.path || a.device != b.device || a.inode != b.inode ||
            a.type != b.type || a.modifiedSeconds != b.modifiedSeconds ||
            a.modifiedNanoseconds != b.modifiedNanoseconds ||
            (a.type == S_IFREG && a.size != b.size)) return false;
    }
    return true;
}

bool restoreStaged(int parent, const QByteArray &staged, const QByteArray &original,
                   QString &error) {
    if (::renameat2(parent, staged.constData(), parent, original.constData(),
                    RENAME_NOREPLACE) == 0) return true;
    error += "; item retained as " + QString::fromLocal8Bit(staged);
    return false;
}

bool stageEntry(int parent, const QByteArray &name, const TrashNode &expected, quint64 mountId,
                QByteArray &staged, QString &error) {
    struct stat current {};
    if (::fstatat(parent, name.constData(), &current, AT_SYMLINK_NOFOLLOW) != 0 ||
        !sameNode(expected, current) ||
        !sameMount(parent, name.constData(), AT_SYMLINK_NOFOLLOW, mountId)) {
        error = "Trash item changed after scan"; return false;
    }
    for (int attempt = 0; attempt < 8; ++attempt) {
        staged = ".syslinuxos-care-" +
                 QByteArray::number(QRandomGenerator::system()->generate64(), 16);
        if (::renameat2(parent, name.constData(), parent, staged.constData(),
                        RENAME_NOREPLACE) == 0) break;
        if (errno != EEXIST) { error = osError(); staged.clear(); return false; }
        staged.clear();
    }
    if (staged.isEmpty()) { error = "Could not reserve a temporary trash name"; return false; }
    if (::fstatat(parent, staged.constData(), &current, AT_SYMLINK_NOFOLLOW) != 0 ||
        !sameNode(expected, current) ||
        !sameMount(parent, staged.constData(), AT_SYMLINK_NOFOLLOW, mountId)) {
        error = "Trash item changed after scan";
        restoreStaged(parent, staged, name, error);
        staged.clear();
        return false;
    }
    return true;
}

bool removeStaged(int parent, const QByteArray &staged, const QByteArray &original,
                  const TrashNode &expected, quint64 mountId, QString &error) {
    struct stat current {};
    if (::fstatat(parent, staged.constData(), &current, AT_SYMLINK_NOFOLLOW) != 0 ||
        !sameNode(expected, current) ||
        !sameMount(parent, staged.constData(), AT_SYMLINK_NOFOLLOW, mountId)) {
        error = "Trash item changed during removal";
    } else if (::unlinkat(parent, staged.constData(),
                          expected.type == S_IFDIR ? AT_REMOVEDIR : 0) == 0) {
        return true;
    } else {
        error = osError();
    }
    restoreStaged(parent, staged, original, error);
    return false;
}

bool removePreviewedChildren(int directory, QVector<TrashNode> nodes,
                            quint64 mountId, QString &error) {
    QHash<QByteArray, TrashNode> identities;
    for (const auto &node : nodes) identities.insert(node.path, node);
    std::sort(nodes.begin(), nodes.end(), [](const auto &a, const auto &b) {
        return a.path < b.path;
    });
    bool removedAny = false;
    for (auto it = nodes.crbegin(); it != nodes.crend(); ++it) {
        const QList<QByteArray> parts = it->path.split('/');
        int parent = directory;
        QVector<int> opened;
        QByteArray prefix;
        for (int index = 0; index + 1 < parts.size(); ++index) {
            prefix = prefix.isEmpty() ? parts[index] : prefix + '/' + parts[index];
            const int next = ::openat(parent, parts[index].constData(),
                                      O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (next < 0) { error = osError(); break; }
            struct stat info {};
            const auto expectedParent = identities.constFind(prefix);
            if (expectedParent == identities.cend() || ::fstat(next, &info) != 0 ||
                !sameNode(expectedParent.value(), info) || !sameMountFd(next, mountId)) {
                ::close(next);
                error = "Trash folder changed during removal";
                break;
            }
            opened.append(next);
            parent = next;
        }
        bool removed = false;
        if (error.isEmpty()) {
            QByteArray staged;
            removed = stageEntry(parent, parts.last(), *it, mountId, staged, error) &&
                      removeStaged(parent, staged, parts.last(), *it, mountId, error);
        }
        for (int fd : opened) ::close(fd);
        if (!removed) {
            if (removedAny) error.prepend("Partial cleanup; some previewed items were removed. ");
            return false;
        }
        removedAny = true;
    }
    return true;
}
}

namespace {
TrashScan scanTrashRoot(const QString &trashRoot, const QString &originalBase,
                        const std::atomic_bool *cancel) {
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
    if (!mountIdAt(root.get(), "", AT_EMPTY_PATH, result.rootMountId)) {
        result.error = "Cannot verify trash mount boundary";
        return result;
    }
    Fd files(::openat(root.get(), "files", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    Fd info(::openat(root.get(), "info", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (files.get() < 0 || info.get() < 0) { result.error = osError(); return result; }
    struct stat filesInfo {}, infoInfo {};
    if (::fstat(files.get(), &filesInfo) != 0 || ::fstat(info.get(), &infoInfo) != 0 ||
        filesInfo.st_dev != rootInfo.st_dev || infoInfo.st_dev != rootInfo.st_dev ||
        !sameMountFd(files.get(), result.rootMountId) ||
        !sameMountFd(info.get(), result.rootMountId)) {
        result.error = "Trash subdirectories are unavailable";
        return result;
    }
    result.filesInode = filesInfo.st_ino;
    result.infoInode = infoInfo.st_ino;
    QDir infoDir(QDir(result.root).filePath("info"));
    for (const QString &filename : infoDir.entryList({"*.trashinfo"}, QDir::Files | QDir::NoSymLinks)) {
        if (cancel && cancel->load()) break;
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
            if (cancel && cancel->load()) break;
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
        TrashEntry entry;
        entry.id = id;
        entry.originalPath = originalPath;
        entry.deletionDate = deletionDate;
        entry.device = payload.st_dev;
        entry.inode = payload.st_ino;
        entry.infoInode = metadata.st_ino;
        entry.modifiedSeconds = payload.st_mtim.tv_sec;
        entry.modifiedNanoseconds = payload.st_mtim.tv_nsec;
        entry.infoModifiedSeconds = metadata.st_mtim.tv_sec;
        entry.infoModifiedNanoseconds = metadata.st_mtim.tv_nsec;
        entry.type = payload.st_mode & S_IFMT;
        entry.directory = S_ISDIR(payload.st_mode);
        entry.complete = (entry.directory || S_ISREG(payload.st_mode) || S_ISLNK(payload.st_mode)) &&
            sameMount(files.get(), name.constData(), AT_SYMLINK_NOFOLLOW, result.rootMountId) &&
            sameMount(info.get(), QFile::encodeName(filename).constData(),
                      AT_SYMLINK_NOFOLLOW, result.rootMountId);
        entry.size = S_ISREG(payload.st_mode) ? quint64(payload.st_size) : 0;
        if (entry.directory && entry.complete) {
            Fd child(::openat(files.get(), name.constData(), O_RDONLY | O_DIRECTORY |
                             O_NOFOLLOW | O_CLOEXEC));
            struct stat opened {};
            if (child.get() < 0 || ::fstat(child.get(), &opened) != 0 ||
                opened.st_dev != payload.st_dev || opened.st_ino != payload.st_ino ||
                !sameMountFd(child.get(), result.rootMountId)) {
                entry.complete = false;
            } else {
                QString scanError;
                entry.complete = captureTree(child.get(), {}, payload.st_dev,
                                             result.rootMountId,
                                             entry.children, entry.size, scanError, cancel);
            }
        }
        if (cancel && cancel->load()) break;
        result.entries.append(entry);
    }
    return result;
}

void inspectMountTrash(int parentFd, const QByteArray &name, uid_t userId,
                       dev_t expectedDevice, quint64 expectedMountId, const QString &trashRoot,
                       const QString &originalBase, QVector<TrashScan> &scans,
                       const std::atomic_bool *cancel) {
    Fd directory(::openat(parentFd, name.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (directory.get() < 0) return;
    struct stat info {};
    if (::fstat(directory.get(), &info) != 0 || info.st_uid != userId ||
        info.st_dev != expectedDevice || !sameMountFd(directory.get(), expectedMountId)) return;
    const TrashScan scan = scanTrashRoot(trashRoot, originalBase, cancel);
    quint64 mountId = 0;
    if (cancel && cancel->load()) return;
    if (scan.error.isEmpty() && scan.rootDevice == quint64(info.st_dev) &&
        scan.rootInode == quint64(info.st_ino) &&
        mountIdAt(directory.get(), "", AT_EMPTY_PATH, mountId) &&
        scan.rootMountId == mountId) scans.append(scan);
}
}

TrashScan scanHomeTrash(const QString &dataHome, const std::atomic_bool *cancel) {
    return scanTrashRoot(QDir(dataHome).filePath("Trash"), dataHome, cancel);
}

QVector<TrashScan> scanTrashLocations(const QString &dataHome, const QStringList &mountRoots,
                                     quint32 userId, const std::atomic_bool *cancel) {
    if (cancel && cancel->load()) return {};
    QVector<TrashScan> scans{scanHomeTrash(dataHome, cancel)};
    QSet<QString> seen;
    seen.insert(QDir(dataHome).filePath("Trash"));
    const QByteArray uid = QByteArray::number(userId);
    for (const QString &mountRoot : mountRoots) {
        if (cancel && cancel->load()) break;
        const QString rootPath = QDir::cleanPath(mountRoot);
        if (!QDir::isAbsolutePath(rootPath)) continue;
        Fd mount(::open(QFile::encodeName(rootPath).constData(),
                        O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (mount.get() < 0) continue;
        struct stat mountInfo {};
        if (::fstat(mount.get(), &mountInfo) != 0) continue;
        quint64 mountId = 0;
        if (!mountIdAt(mount.get(), "", AT_EMPTY_PATH, mountId)) continue;
        Fd shared(::openat(mount.get(), ".Trash", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (shared.get() >= 0) {
            struct stat info {};
            if (::fstat(shared.get(), &info) == 0 && info.st_dev == mountInfo.st_dev &&
                sameMountFd(shared.get(), mountId) &&
                (info.st_mode & S_ISVTX)) {
                const QString path = QDir(rootPath).filePath(".Trash/" + QString::number(userId));
                if (!seen.contains(path)) {
                    inspectMountTrash(shared.get(), uid, userId, mountInfo.st_dev, mountId, path,
                                      rootPath, scans, cancel);
                    seen.insert(path);
                }
            }
        }
        const QByteArray privateName = ".Trash-" + uid;
        const QString privatePath = QDir(rootPath).filePath(QString::fromLocal8Bit(privateName));
        if (!seen.contains(privatePath)) {
            inspectMountTrash(mount.get(), privateName, userId, mountInfo.st_dev, mountId,
                              privatePath, rootPath, scans, cancel);
            seen.insert(privatePath);
        }
    }
    return scans;
}

bool removeTrashEntry(const TrashScan &scan, const TrashEntry &entry, QString &error) {
    error.clear();
    if (scan.root.isEmpty() || !safeName(QFile::encodeName(entry.id)) || !entry.complete) {
        error = "Invalid trash entry";
        return false;
    }
    Fd root(::open(QFile::encodeName(scan.root).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (root.get() < 0) { error = osError(); return false; }
    struct stat rootInfo {};
    if (::fstat(root.get(), &rootInfo) != 0 || quint64(rootInfo.st_dev) != scan.rootDevice ||
        quint64(rootInfo.st_ino) != scan.rootInode ||
        !sameMountFd(root.get(), scan.rootMountId)) {
        error = "Trash directory changed after scan";
        return false;
    }
    Fd files(::openat(root.get(), "files", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    Fd info(::openat(root.get(), "info", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (files.get() < 0 || info.get() < 0) { error = osError(); return false; }
    struct stat filesInfo {}, infoInfo {};
    if (::fstat(files.get(), &filesInfo) != 0 || ::fstat(info.get(), &infoInfo) != 0 ||
        quint64(filesInfo.st_dev) != scan.rootDevice ||
        quint64(infoInfo.st_dev) != scan.rootDevice ||
        quint64(filesInfo.st_ino) != scan.filesInode ||
        quint64(infoInfo.st_ino) != scan.infoInode ||
        !sameMountFd(files.get(), scan.rootMountId) ||
        !sameMountFd(info.get(), scan.rootMountId)) {
        error = "Trash directories changed after scan";
        return false;
    }
    const QByteArray name = QFile::encodeName(entry.id);
    const QByteArray infoName = name + ".trashinfo";
    struct stat payload {}, metadata {};
    if (::fstatat(files.get(), name.constData(), &payload, AT_SYMLINK_NOFOLLOW) != 0 ||
        ::fstatat(info.get(), infoName.constData(), &metadata, AT_SYMLINK_NOFOLLOW) != 0 ||
        !sameMount(files.get(), name.constData(), AT_SYMLINK_NOFOLLOW, scan.rootMountId) ||
        !sameMount(info.get(), infoName.constData(), AT_SYMLINK_NOFOLLOW, scan.rootMountId) ||
        quint64(payload.st_dev) != entry.device || quint64(payload.st_ino) != entry.inode ||
        quint64(metadata.st_ino) != entry.infoInode || !S_ISREG(metadata.st_mode) ||
        quint32(payload.st_mode & S_IFMT) != entry.type ||
        payload.st_mtim.tv_sec != entry.modifiedSeconds ||
        payload.st_mtim.tv_nsec != entry.modifiedNanoseconds ||
        (!entry.directory && S_ISREG(payload.st_mode) && quint64(payload.st_size) != entry.size) ||
        metadata.st_mtim.tv_sec != entry.infoModifiedSeconds ||
        metadata.st_mtim.tv_nsec != entry.infoModifiedNanoseconds) {
        error = "Trash entry changed after scan";
        return false;
    }
    const TrashNode expected{name, entry.device, entry.inode, entry.size,
                             entry.modifiedSeconds, entry.modifiedNanoseconds, entry.type};
    QByteArray staged;
    if (!stageEntry(files.get(), name, expected, scan.rootMountId, staged, error)) return false;
    if (entry.directory) {
        Fd directory(::openat(files.get(), staged.constData(), O_RDONLY | O_DIRECTORY |
                              O_NOFOLLOW | O_CLOEXEC));
        QVector<TrashNode> current;
        quint64 bytes = 0;
        QString treeError;
        if (directory.get() < 0 ||
            !sameMountFd(directory.get(), scan.rootMountId) ||
            !captureTree(directory.get(), {}, payload.st_dev, scan.rootMountId,
                         current, bytes, treeError) ||
            !sameTree(entry.children, current)) {
            error = "Trash folder changed after scan";
            restoreStaged(files.get(), staged, name, error);
            return false;
        }
        if (!removePreviewedChildren(directory.get(), entry.children, scan.rootMountId, error)) {
            restoreStaged(files.get(), staged, name, error);
            return false;
        }
    }
    if (!removeStaged(files.get(), staged, name, expected, scan.rootMountId, error)) return false;
    const TrashNode expectedInfo = nodeFromStat(infoName, metadata);
    QByteArray stagedInfo;
    if (!stageEntry(info.get(), infoName, expectedInfo, scan.rootMountId, stagedInfo, error) ||
        !removeStaged(info.get(), stagedInfo, infoName, expectedInfo, scan.rootMountId, error)) {
        error.prepend("Trash payload removed; metadata cleanup failed. ");
        return false;
    }
    return true;
}
