#include "services.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

QVector<ServiceEntry> parseServices(const QByteArray &json, bool user, QString &error) {
    error.clear();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        error = "Invalid service list";
        return {};
    }
    QVector<ServiceEntry> entries;
    for (const auto &value : document.array()) {
        const QJsonObject item = value.toObject();
        const QString unit = item.value("unit").toString();
        if (!unit.endsWith(".service")) continue;
        entries.append({unit, item.value("active").toString(),
                        item.value("description").toString(), user});
    }
    return entries;
}

QVector<ServiceEntry> listServices(bool user, QString &error) {
    error.clear();
    QProcess process;
    QStringList args;
    if (user) args << "--user";
    args << "list-units" << "--type=service" << "--all" << "--no-legend"
         << "--no-pager" << "--plain" << "--output=json";
    process.start("systemctl", args);
    if (!process.waitForStarted(1000) || !process.waitForFinished(3000)) {
        process.kill();
        process.waitForFinished();
        error = "Service manager did not respond";
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (error.isEmpty()) error = "Could not read services";
        return {};
    }
    return parseServices(process.readAllStandardOutput(), user, error);
}

bool setUserServiceRunning(const QString &unit, bool running, QString &error) {
    static const QRegularExpression allowed("^[A-Za-z0-9_.@:-]+\\.service$");
    error.clear();
    if (!allowed.match(unit).hasMatch()) {
        error = "Invalid service name";
        return false;
    }
    QProcess process;
    process.start("systemctl", {"--user", running ? "start" : "stop", unit});
    if (!process.waitForStarted(1000) || !process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished();
        error = "Service action timed out";
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (error.isEmpty()) error = "Service action failed";
        return false;
    }
    return true;
}
