#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include "scanner.h"

enum class BrowserKind { Firefox, Chromium, Chrome };

struct BrowserCache {
    BrowserKind kind;
    QString profile;
    QString path;
};

enum class BrowserDataKind { Cookies, History };

struct BrowserData {
    BrowserKind browser;
    BrowserDataKind kind;
    QString profile;
    QString root;
    QStringList paths;
};

QVector<BrowserCache> discoverBrowserCaches(const QString &cacheHome);
QVector<BrowserData> discoverBrowserData(const QString &configHome, const QString &homePath);
ScanResult scanBrowserData(const BrowserData &data);
QString browserDataFingerprint(const ScanResult &scan);
bool browserRunning(BrowserKind kind);
QString browserName(BrowserKind kind);
