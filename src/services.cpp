#include "services.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
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
        entries.append({unit, item.value("active").toString(), {},
                        item.value("description").toString(), user});
    }
    return entries;
}

void applyServiceFileStates(QVector<ServiceEntry> &entries, const QByteArray &json, bool user,
                            QString &error) {
    error.clear();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        error = "Invalid service file list";
        return;
    }
    QMap<QString, QString> states;
    for (const auto &value : document.array()) {
        const QJsonObject item = value.toObject();
        const QString unit = item.value("unit_file").toString();
        if (unit.endsWith(".service")) states.insert(unit, item.value("state").toString());
    }
    for (auto &entry : entries) {
        entry.enabled = states.take(entry.unit);
        if (entry.enabled.isEmpty()) entry.enabled = "unknown";
    }
    for (auto it = states.cbegin(); it != states.cend(); ++it)
        entries.append({it.key(), "inactive", it.value(), {}, user});
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
    auto entries = parseServices(process.readAllStandardOutput(), user, error);
    if (!error.isEmpty()) return {};
    QProcess files;
    QStringList fileArgs;
    if (user) fileArgs << "--user";
    fileArgs << "list-unit-files" << "--type=service" << "--no-legend" << "--no-pager"
             << "--output=json";
    files.start("systemctl", fileArgs);
    if (!files.waitForStarted(1000) || !files.waitForFinished(3000) || files.exitCode() != 0) {
        files.kill();
        files.waitForFinished();
        error = "Could not read service startup settings";
        return {};
    }
    applyServiceFileStates(entries, files.readAllStandardOutput(), user, error);
    return entries;
}

namespace {
bool validUnit(const QString &unit) {
    static const QRegularExpression allowed("^[A-Za-z0-9_.@:-]+\\.service$");
    return allowed.match(unit).hasMatch();
}

bool userServiceAction(const QString &unit, const QString &action, QString &error) {
    error.clear();
    if (!validUnit(unit)) {
        error = "Invalid service name";
        return false;
    }
    QProcess process;
    process.start("systemctl", {"--user", action, unit});
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
}

bool setUserServiceRunning(const QString &unit, bool running, QString &error) {
    return userServiceAction(unit, running ? "start" : "stop", error);
}

bool setUserServiceEnabled(const QString &unit, bool enabled, QString &error) {
    return userServiceAction(unit, enabled ? "enable" : "disable", error);
}
