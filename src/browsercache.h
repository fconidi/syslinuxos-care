#pragma once

#include <QString>
#include <QVector>

enum class BrowserKind { Firefox, Chromium, Chrome };

struct BrowserCache {
    BrowserKind kind;
    QString profile;
    QString path;
};

QVector<BrowserCache> discoverBrowserCaches(const QString &cacheHome);
bool browserRunning(BrowserKind kind);
QString browserName(BrowserKind kind);
