#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct AutostartEntry {
    QString id;
    QString name;
    QString path;
    bool enabled = false;
    bool userEntry = false;
};

QVector<AutostartEntry> listAutostart(const QString &userConfigHome,
                                     const QStringList &systemConfigDirs);
