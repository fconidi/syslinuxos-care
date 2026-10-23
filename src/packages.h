#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

struct PackageEntry {
    QString name;
    QString version;
};

QVector<PackageEntry> parseInstalledPackages(const QByteArray &output);
QVector<PackageEntry> installedPackages(QString &error);
QStringList parseRemovalPlan(const QByteArray &output);
QStringList simulatePackageRemoval(const QString &package, QString &error);
