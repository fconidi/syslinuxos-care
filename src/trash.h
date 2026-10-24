#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>

struct TrashNode {
    QByteArray path;
    quint64 device = 0;
    quint64 inode = 0;
    quint64 size = 0;
    qint64 modifiedSeconds = 0;
    qint64 modifiedNanoseconds = 0;
    quint32 type = 0;
};

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
    quint32 type = 0;
    bool complete = true;
    QVector<TrashNode> children;
};

struct TrashScan {
    QString root;
    quint64 rootDevice = 0;
    quint64 rootInode = 0;
    quint64 filesInode = 0;
    quint64 infoInode = 0;
    QVector<TrashEntry> entries;
    QString error;
};

TrashScan scanHomeTrash(const QString &dataHome, const std::atomic_bool *cancel = nullptr);
QVector<TrashScan> scanTrashLocations(const QString &dataHome, const QStringList &mountRoots,
                                     quint32 userId, const std::atomic_bool *cancel = nullptr);
bool removeTrashEntry(const TrashScan &scan, const TrashEntry &entry, QString &error);
