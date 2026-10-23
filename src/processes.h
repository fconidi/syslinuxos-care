#pragma once

#include <QString>
#include <QVector>

struct ProcessEntry {
    int pid = 0;
    unsigned int uid = 0;
    QString name;
    quint64 residentBytes = 0;
    quint64 startTicks = 0;
};

QVector<ProcessEntry> listProcesses(const QString &procRoot = "/proc");
bool terminateOwnProcess(const ProcessEntry &entry, QString &error,
                         const QString &procRoot = "/proc");
