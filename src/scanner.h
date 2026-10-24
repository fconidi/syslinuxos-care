#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>

struct ScanItem {
    QString relativePath;
    quint64 size = 0;
    quint64 device = 0;
    quint64 inode = 0;
    qint64 modifiedSeconds = 0;
    qint64 modifiedNanoseconds = 0;
};

struct ScanResult {
    QString root;
    quint64 rootDevice = 0;
    quint64 rootInode = 0;
    quint64 rootMountId = 0;
    QVector<ScanItem> items;
    QString error;
    bool truncated = false;
    bool cancelled = false;
};

ScanResult scanRegularFiles(const QString &root, int maxItems = 50000,
                            const std::atomic_bool *cancel = nullptr);
ScanResult scanNamedFiles(const QString &root, const QStringList &relativePaths,
                          const std::atomic_bool *cancel = nullptr);
bool removeScannedFile(const ScanResult &scan, const ScanItem &item, QString &error);
