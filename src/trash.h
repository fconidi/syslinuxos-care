#pragma once

#include <QString>
#include <QVector>

struct TrashEntry {
    QString id;
    QString originalPath;
    QString deletionDate;
    quint64 size = 0;
    quint64 device = 0;
    quint64 inode = 0;
    quint64 infoInode = 0;
    qint64 modifiedSeconds = 0;
    qint64 modifiedNanoseconds = 0;
    qint64 infoModifiedSeconds = 0;
    qint64 infoModifiedNanoseconds = 0;
    bool directory = false;
};

struct TrashScan {
    QString root;
    quint64 rootDevice = 0;
    quint64 rootInode = 0;
    QVector<TrashEntry> entries;
    QString error;
};

TrashScan scanHomeTrash(const QString &dataHome);
bool removeTrashEntry(const TrashScan &scan, const TrashEntry &entry, QString &error);
