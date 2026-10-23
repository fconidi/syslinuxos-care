#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

struct ServiceEntry {
    QString unit;
    QString active;
    QString description;
    bool user = false;
};

QVector<ServiceEntry> parseServices(const QByteArray &json, bool user, QString &error);
QVector<ServiceEntry> listServices(bool user, QString &error);
bool setUserServiceRunning(const QString &unit, bool running, QString &error);
