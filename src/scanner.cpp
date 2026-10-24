#include "scanner.h"

#include <QByteArray>
#include <QFile>
#include <QRandomGenerator>

#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <cstdio>
#include <linux/fs.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
class Fd {
public:
    explicit Fd(int value = -1) : value_(value) {}
    ~Fd() { if (value_ >= 0) ::close(value_); }
    Fd(const Fd &) = delete;
    Fd &operator=(const Fd &) = delete;
    int get() const { return value_; }
private:
    int value_;
};

QString osError() { return QString::fromLocal8Bit(std::strerror(errno)); }

bool safeComponent(const QByteArray &name) {
    return !name.isEmpty() && name != "." && name != ".." && !name.contains('/') && !name.contains('\0');
}

void walk(int directory, const QByteArray &prefix, quint64 device, ScanResult &result, int maxItems,
          const std::atomic_bool *cancel) {
    const int streamFd = ::dup(directory);
    if (streamFd < 0) { result.error = osError(); return; }
    DIR *stream = ::fdopendir(streamFd);
    if (!stream) { result.error = osError(); ::close(streamFd); return; }
    while (dirent *entry = ::readdir(stream)) {
        if (cancel && cancel->load()) { result.cancelled = true; break; }
        const QByteArray name(entry->d_name);
        if (!safeComponent(name)) continue;
        struct stat info {};
        if (::fstatat(directory, name.constData(), &info, AT_SYMLINK_NOFOLLOW) != 0) continue;
        if (quint64(info.st_dev) != device) continue;
        const QByteArray relative = prefix.isEmpty() ? name : prefix + '/' + name;
        if (S_ISDIR(info.st_mode)) {
            Fd child(::openat(directory, name.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
            if (child.get() >= 0) walk(child.get(), relative, device, result, maxItems, cancel);
        } else if (S_ISREG(info.st_mode)) {
            if (result.items.size() >= maxItems) { result.truncated = true; break; }
            result.items.append({QString::fromLocal8Bit(relative), quint64(info.st_size),
                                 quint64(info.st_dev), quint64(info.st_ino), info.st_mtim.tv_sec,
                                 info.st_mtim.tv_nsec});
        }
        if (result.truncated || result.cancelled || !result.error.isEmpty()) break;
    }
    ::closedir(stream);
}
}

ScanResult scanRegularFiles(const QString &root, int maxItems, const std::atomic_bool *cancel) {
    ScanResult result;
    if (maxItems <= 0) { result.error = QStringLiteral("Invalid scan limit"); return result; }
    const QByteArray path = QFile::encodeName(root);
    Fd directory(::open(path.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (directory.get() < 0) { result.error = osError(); return result; }
    struct stat info {};
    if (::fstat(directory.get(), &info) != 0) { result.error = osError(); return result; }
    result.root = root;
    result.rootDevice = quint64(info.st_dev);
    result.rootInode = quint64(info.st_ino);
    walk(directory.get(), {}, result.rootDevice, result, maxItems, cancel);
    return result;
}

ScanResult scanNamedFiles(const QString &root, const QStringList &relativePaths) {
    ScanResult result;
    if (relativePaths.isEmpty() || relativePaths.size() > 16) {
        result.error = QStringLiteral("Invalid file selection");
        return result;
    }
    const QByteArray path = QFile::encodeName(root);
    Fd directory(::open(path.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    struct stat rootInfo {};
    if (directory.get() < 0 || ::fstat(directory.get(), &rootInfo) != 0) {
        result.error = osError();
        return result;
    }
    result.root = root;
    result.rootDevice = quint64(rootInfo.st_dev);
    result.rootInode = quint64(rootInfo.st_ino);
    for (const auto &relativePath : relativePaths) {
        const QList<QByteArray> components = QFile::encodeName(relativePath).split('/');
        for (const auto &part : components) {
            if (!safeComponent(part)) {
                result.error = QStringLiteral("Invalid relative path");
                return result;
            }
        }
        int parent = directory.get();
        QVector<int> opened;
        bool unavailable = false;
        for (int i = 0; i + 1 < components.size(); ++i) {
            const int next = ::openat(parent, components[i].constData(),
                                      O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (next < 0) { unavailable = true; break; }
            struct stat info {};
            if (::fstat(next, &info) != 0 || quint64(info.st_dev) != result.rootDevice) {
                ::close(next);
                unavailable = true;
                break;
            }
            opened.append(next);
            parent = next;
        }
        if (!unavailable) {
            struct stat info {};
            if (::fstatat(parent, components.last().constData(), &info,
                          AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(info.st_mode) &&
                quint64(info.st_dev) == result.rootDevice) {
                result.items.append({relativePath, quint64(info.st_size),
                                     quint64(info.st_dev), quint64(info.st_ino),
                                     info.st_mtim.tv_sec, info.st_mtim.tv_nsec});
            }
        }
        for (int fd : opened) ::close(fd);
    }
    return result;
}

bool removeScannedFile(const ScanResult &scan, const ScanItem &item, QString &error) {
    if (scan.root.isEmpty() || item.relativePath.isEmpty() || item.device != scan.rootDevice) {
        error = QStringLiteral("Invalid scan item"); return false;
    }
    const QByteArray root = QFile::encodeName(scan.root);
    Fd directory(::open(root.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    struct stat rootInfo {};
    if (directory.get() < 0 || ::fstat(directory.get(), &rootInfo) != 0 ||
        quint64(rootInfo.st_dev) != scan.rootDevice || quint64(rootInfo.st_ino) != scan.rootInode) {
        error = QStringLiteral("Scan directory changed"); return false;
    }
    const QList<QByteArray> components = QFile::encodeName(item.relativePath).split('/');
    for (const auto &part : components) {
        if (!safeComponent(part)) { error = QStringLiteral("Invalid path"); return false; }
    }
    int parent = directory.get();
    QVector<int> opened;
    for (int i = 0; i + 1 < components.size(); ++i) {
        int next = ::openat(parent, components[i].constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0) { error = osError(); for (int fd : opened) ::close(fd); return false; }
        opened.append(next);
        parent = next;
    }
    struct stat current {};
    const auto &name = components.last();
    const bool same = ::fstatat(parent, name.constData(), &current, AT_SYMLINK_NOFOLLOW) == 0 &&
        S_ISREG(current.st_mode) && quint64(current.st_dev) == item.device &&
        quint64(current.st_ino) == item.inode && quint64(current.st_size) == item.size &&
        current.st_mtim.tv_sec == item.modifiedSeconds &&
        current.st_mtim.tv_nsec == item.modifiedNanoseconds;
    bool removed = false;
    if (!same) {
        error = QStringLiteral("File changed after scan");
    } else {
        QByteArray stagedName;
        for (int attempt = 0; attempt < 8; ++attempt) {
            stagedName = ".syslinuxos-care-" +
                         QByteArray::number(QRandomGenerator::system()->generate64(), 16);
            if (::renameat2(parent, name.constData(), parent, stagedName.constData(),
                            RENAME_NOREPLACE) == 0) break;
            if (errno != EEXIST) { error = osError(); stagedName.clear(); break; }
            stagedName.clear();
        }
        if (stagedName.isEmpty() && error.isEmpty())
            error = QStringLiteral("Could not reserve a temporary file name");
        if (!stagedName.isEmpty()) {
            struct stat staged {};
            const bool unchanged = ::fstatat(parent, stagedName.constData(), &staged,
                                             AT_SYMLINK_NOFOLLOW) == 0 &&
                S_ISREG(staged.st_mode) && quint64(staged.st_dev) == item.device &&
                quint64(staged.st_ino) == item.inode && quint64(staged.st_size) == item.size &&
                staged.st_mtim.tv_sec == item.modifiedSeconds &&
                staged.st_mtim.tv_nsec == item.modifiedNanoseconds;
            if (unchanged) {
                removed = ::unlinkat(parent, stagedName.constData(), 0) == 0;
                if (!removed) error = osError();
            } else {
                error = QStringLiteral("File changed after scan");
            }
            if (!removed) {
                if (::renameat2(parent, stagedName.constData(), parent, name.constData(),
                                RENAME_NOREPLACE) != 0)
                    error += QStringLiteral("; item retained as ") +
                             QString::fromLocal8Bit(stagedName);
            }
        }
    }
    for (int fd : opened) ::close(fd);
    return removed;
}
