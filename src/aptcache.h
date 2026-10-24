#pragma once

#include "scanner.h"

#include <QStringList>

QString aptArchiveDirectory();
bool validAptArchiveName(const QString &name);
ScanResult scanAptArchives(const QString &root);
QString aptArchiveFingerprint(const ScanResult &scan);
bool removeAptArchives(const QString &root, const QStringList &selected,
                       const QString &fingerprint, QStringList &outcomes);
