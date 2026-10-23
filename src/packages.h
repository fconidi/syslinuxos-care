#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

struct PackageEntry {
    QString name;
    QString version;
};

struct PackageRemovalPlan {
    QStringList removals;
    QStringList otherActions;
};

QVector<PackageEntry> parseInstalledPackages(const QByteArray &output);
QVector<PackageEntry> installedPackages(QString &error);
PackageRemovalPlan parseRemovalPlan(const QByteArray &output);
PackageRemovalPlan simulatePackageRemoval(const QString &package, QString &error);
