#pragma once

#include <QString>
#include <QVector>

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
    QVector<ScanItem> items;
    QString error;
    bool truncated = false;
};

ScanResult scanRegularFiles(const QString &root, int maxItems = 50000);
bool removeScannedFile(const ScanResult &scan, const ScanItem &item, QString &error);
