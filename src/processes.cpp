#include "processes.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <csignal>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
bool readProcess(const QString &procRoot, int pid, ProcessEntry &entry) {
    const QString base = QDir(procRoot).filePath(QString::number(pid));
    QFileInfo directory(base);
    if (!directory.isDir() || directory.isSymLink()) return false;
    QFile status(QDir(base).filePath("status"));
    QFile stat(QDir(base).filePath("stat"));
    if (!status.open(QIODevice::ReadOnly | QIODevice::Text) || !stat.open(QIODevice::ReadOnly))
        return false;
    entry = {};
    entry.pid = pid;
    bool hasUid = false;
    while (!status.atEnd()) {
        const QByteArray line = status.readLine();
        if (line.startsWith("Name:")) entry.name = QString::fromUtf8(line.mid(5).trimmed());
        if (line.startsWith("Uid:")) {
            const auto parts = QString::fromUtf8(line.mid(4)).simplified().split(' ');
            if (!parts.isEmpty()) {
                bool ok = false;
                entry.uid = parts.first().toUInt(&ok);
                hasUid = ok;
            }
        }
        if (line.startsWith("VmRSS:")) {
            const auto parts = QString::fromUtf8(line.mid(6)).simplified().split(' ');
            bool ok = false;
            const quint64 kib = parts.first().toULongLong(&ok);
            if (ok) entry.residentBytes = kib * 1024;
        }
    }
    const QByteArray data = stat.readAll();
    const int close = data.lastIndexOf(')');
    if (close < 0) return false;
    const QList<QByteArray> fields = data.mid(close + 1).simplified().split(' ');
    if (fields.size() <= 19) return false;
    bool ok = false;
    entry.startTicks = fields[19].toULongLong(&ok);
    return hasUid && ok && !entry.name.isEmpty();
}
}

QVector<ProcessEntry> listProcesses(const QString &procRoot) {
    QVector<ProcessEntry> result;
    QDir directory(procRoot);
    const auto ids = directory.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const QString &id : ids) {
        bool ok = false;
        const int pid = id.toInt(&ok);
        if (!ok || pid <= 0) continue;
        ProcessEntry entry;
        if (readProcess(procRoot, pid, entry)) result.append(entry);
    }
    return result;
}

bool terminateOwnProcess(const ProcessEntry &entry, QString &error, const QString &procRoot) {
    error.clear();
    if (procRoot != "/proc" || getuid() == 0 || entry.pid <= 1 || entry.pid == getpid()) {
        error = "Process cannot be terminated";
        return false;
    }
    const int pidfd = static_cast<int>(syscall(SYS_pidfd_open, entry.pid, 0));
    if (pidfd < 0) {
        error = "Process is no longer available";
        return false;
    }
    ProcessEntry current;
    if (!readProcess(procRoot, entry.pid, current) || current.startTicks != entry.startTicks ||
        current.uid != entry.uid || current.uid != getuid()) {
        close(pidfd);
        error = "Process changed or is not owned by this user";
        return false;
    }
    const bool sent = syscall(SYS_pidfd_send_signal, pidfd, SIGTERM, nullptr, 0) == 0;
    close(pidfd);
    if (!sent) {
        error = "Could not send termination request";
        return false;
    }
    return true;
}
