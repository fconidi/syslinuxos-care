#include "metrics.h"

#include <QFile>
#include <QStorageInfo>
#include <QDateTime>
#include <QStringList>

namespace {
QByteArray readFile(const char *path) {
    QFile file(QString::fromLocal8Bit(path));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
}

bool MetricsReader::parseCpu(const QByteArray &data, quint64 &busy, quint64 &total) {
    const auto line = data.split('\n').value(0).simplified();
    const auto fields = line.split(' ');
    if (fields.size() < 5 || fields.first() != "cpu") return false;
    quint64 values[8]{};
    // guest and guest_nice are already included in user and nice by Linux.
    const int count = qMin(fields.size() - 1, 8);
    for (int i = 0; i < count; ++i) {
        bool ok = false;
        values[i] = fields[i + 1].toULongLong(&ok);
        if (!ok) return false;
    }
    const quint64 idle = values[3] + values[4];
    total = 0;
    for (int i = 0; i < count; ++i) total += values[i];
    busy = total - idle;
    return total > 0;
}

bool MetricsReader::parseMemory(const QByteArray &data, quint64 &total, quint64 &available) {
    total = available = 0;
    for (const auto &line : data.split('\n')) {
        const auto parts = line.simplified().split(' ');
        if (parts.size() < 2) continue;
        bool ok = false;
        const quint64 value = parts[1].toULongLong(&ok);
        if (!ok) continue;
        if (parts[0] == "MemTotal:") total = value * 1024;
        if (parts[0] == "MemAvailable:") available = value * 1024;
    }
    return total > 0 && available <= total;
}

bool MetricsReader::parseNetwork(const QByteArray &data, quint64 &rx, quint64 &tx) {
    rx = tx = 0;
    bool found = false;
    for (const auto &line : data.split('\n')) {
        const int colon = line.indexOf(':');
        if (colon < 0 || line.left(colon).trimmed() == "lo") continue;
        const auto parts = line.mid(colon + 1).simplified().split(' ');
        if (parts.size() < 16) continue;
        bool rxOk = false, txOk = false;
        const quint64 rxValue = parts[0].toULongLong(&rxOk);
        const quint64 txValue = parts[8].toULongLong(&txOk);
        if (rxOk && txOk) { rx += rxValue; tx += txValue; found = true; }
    }
    return found;
}

Metrics MetricsReader::sample() {
    Metrics result;
    quint64 busy = 0, total = 0;
    if (parseCpu(readFile("/proc/stat"), busy, total)) {
        if (previousTotal_ && total > previousTotal_ && busy >= previousBusy_)
            result.cpuPercent = 100.0 * double(busy - previousBusy_) / double(total - previousTotal_);
        previousBusy_ = busy;
        previousTotal_ = total;
    }
    quint64 available = 0;
    if (parseMemory(readFile("/proc/meminfo"), result.memoryTotal, available))
        result.memoryUsed = result.memoryTotal - available;
    const QStorageInfo disk(QStringLiteral("/"));
    if (disk.isValid() && disk.isReady()) {
        result.diskTotal = quint64(disk.bytesTotal());
        result.diskUsed = result.diskTotal - quint64(disk.bytesFree());
    }
    quint64 rx = 0, tx = 0;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (parseNetwork(readFile("/proc/net/dev"), rx, tx)) {
        const qint64 elapsed = now - previousTimeMs_;
        if (previousTimeMs_ && elapsed > 0 && rx >= previousRx_ && tx >= previousTx_) {
            result.rxBytesPerSecond = (rx - previousRx_) * 1000 / quint64(elapsed);
            result.txBytesPerSecond = (tx - previousTx_) * 1000 / quint64(elapsed);
        }
        previousRx_ = rx;
        previousTx_ = tx;
        previousTimeMs_ = now;
    }
    return result;
}

QString formatBytes(quint64 bytes) {
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = double(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) { value /= 1024.0; ++unit; }
    return QString::number(value, unit ? 'f' : 'f', unit ? 1 : 0) + " " + units[unit];
}
