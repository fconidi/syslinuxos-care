#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

struct ServiceEntry {
    QString unit;
    QString active;
    QString enabled;
    QString description;
    bool user = false;
};

QVector<ServiceEntry> parseServices(const QByteArray &json, bool user, QString &error);
void applyServiceFileStates(QVector<ServiceEntry> &entries, const QByteArray &json, bool user,
                            QString &error);
QVector<ServiceEntry> listServices(bool user, QString &error);
bool validServiceUnit(const QString &unit);
bool setUserServiceRunning(const QString &unit, bool running, QString &error);
bool setUserServiceEnabled(const QString &unit, bool enabled, QString &error);
