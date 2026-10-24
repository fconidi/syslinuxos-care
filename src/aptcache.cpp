#include "aptcache.h"

#include <QCryptographicHash>
#include <QFile>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <fcntl.h>
#include <unistd.h>

QString aptArchiveDirectory() { return QStringLiteral("/var/cache/apt/archives"); }

bool validAptArchiveName(const QString &name) {
    static const QRegularExpression allowed("^[A-Za-z0-9][A-Za-z0-9+~_.:%-]*\\.deb$");
    return name.size() <= 255 && allowed.match(name).hasMatch();
}

ScanResult scanAptArchives(const QString &root) {
    ScanResult scan = scanRegularFiles(root, 5001);
    if (!scan.error.isEmpty()) return scan;
    if (scan.truncated) {
        scan.error = QStringLiteral("APT archive scan limit reached");
        scan.items.clear();
        return scan;
    }
    scan.items.erase(std::remove_if(scan.items.begin(), scan.items.end(), [](const ScanItem &item) {
        return !validAptArchiveName(item.relativePath);
    }), scan.items.end());
    std::sort(scan.items.begin(), scan.items.end(), [](const ScanItem &left, const ScanItem &right) {
        return left.relativePath < right.relativePath;
    });
    return scan;
}

QString aptArchiveFingerprint(const ScanResult &scan) {
    if (!scan.error.isEmpty() || scan.truncated || scan.root.isEmpty()) return {};
    QByteArray data;
    auto append = [&data](const QByteArray &part) {
        data += QByteArray::number(part.size()) + ':' + part;
    };
    append(scan.root.toUtf8());
    append(QByteArray::number(scan.rootDevice));
    append(QByteArray::number(scan.rootInode));
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

bool removeAptArchives(const QString &root, const QStringList &selected,
                       const QString &fingerprint, QStringList &outcomes) {
    outcomes.clear();
    static const QRegularExpression hex("^[a-f0-9]{64}$");
    if (selected.isEmpty() || selected.size() > 5000 || !hex.match(fingerprint).hasMatch()) {
        outcomes << QStringLiteral("Invalid selection");
        return false;
    }
    QSet<QString> unique;
    for (const auto &name : selected) {
        if (!validAptArchiveName(name) || unique.contains(name)) {
            outcomes << QStringLiteral("Invalid archive name or duplicate selection");
            return false;
        }
        unique.insert(name);
    }
    const QByteArray lockPath = QFile::encodeName(root + QStringLiteral("/lock"));
    const int lockFd = ::open(lockPath.constData(), O_RDWR | O_NOFOLLOW | O_CLOEXEC);
    if (lockFd < 0) {
        outcomes << QStringLiteral("Could not open the APT archive lock");
        return false;
    }
    struct flock lock {};
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    if (::fcntl(lockFd, F_SETLK, &lock) != 0) {
        ::close(lockFd);
        outcomes << QStringLiteral("APT archive cache is in use");
        return false;
    }
    const ScanResult scan = scanAptArchives(root);
    if (!scan.error.isEmpty() || aptArchiveFingerprint(scan) != fingerprint) {
        ::close(lockFd);
        outcomes << QStringLiteral("APT archives changed; refresh the preview");
        return false;
    }
    bool allRemoved = true;
    for (const auto &item : scan.items) {
        if (!unique.contains(item.relativePath)) continue;
        QString error;
        if (removeScannedFile(scan, item, error))
            outcomes << item.relativePath + QStringLiteral(": removed");
        else {
            outcomes << item.relativePath + QStringLiteral(": ") + error;
            allRemoved = false;
        }
        unique.remove(item.relativePath);
    }
    if (!unique.isEmpty()) {
        ::close(lockFd);
        outcomes << QStringLiteral("Selected archive missing from preview");
        return false;
    }
    ::close(lockFd);
    return allRemoved;
}
