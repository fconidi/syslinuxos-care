#pragma once

#include <QByteArray>
#include <QString>

struct Metrics {
    double cpuPercent = -1;
    bool cpuAvailable = false;
    quint64 memoryTotal = 0;
    quint64 memoryUsed = 0;
    quint64 diskTotal = 0;
    quint64 diskUsed = 0;
    quint64 rxBytesPerSecond = 0;
    quint64 txBytesPerSecond = 0;
    bool networkAvailable = false;
    bool networkRateAvailable = false;
};

class MetricsReader {
public:
    static bool parseCpu(const QByteArray &data, quint64 &busy, quint64 &total);
    static bool parseMemory(const QByteArray &data, quint64 &total, quint64 &available);
    static bool parseNetwork(const QByteArray &data, quint64 &rx, quint64 &tx);
    Metrics sample();

private:
    quint64 previousBusy_ = 0;
    quint64 previousTotal_ = 0;
    quint64 previousRx_ = 0;
    quint64 previousTx_ = 0;
    qint64 previousTimeMs_ = 0;
};

QString formatBytes(quint64 bytes);
