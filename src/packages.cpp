#include "packages.h"

#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QCryptographicHash>

namespace {
bool run(const QString &program, const QStringList &args, QByteArray &output, QString &error,
         int timeout = 15000) {
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("LC_ALL", "C");
    process.setProcessEnvironment(environment);
    process.start(program, args);
    if (!process.waitForStarted(1000) || !process.waitForFinished(timeout)) {
        process.kill();
        process.waitForFinished();
        error = program + " did not respond";
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (error.isEmpty()) error = program + " failed";
        return false;
    }
    output = process.readAllStandardOutput();
    return true;
}
}

QVector<PackageEntry> parseInstalledPackages(const QByteArray &output) {
    QVector<PackageEntry> entries;
    for (const QByteArray &line : output.split('\n')) {
        const auto parts = line.split('\t');
        if (parts.size() != 3 || !parts[0].startsWith("ii")) continue;
        entries.append({QString::fromUtf8(parts[1]), QString::fromUtf8(parts[2])});
    }
    return entries;
}

QVector<PackageEntry> installedPackages(QString &error) {
    error.clear();
    QByteArray output;
    if (!run("/usr/bin/dpkg-query", {"-W", "-f=${db:Status-Abbrev}\\t${binary:Package}\\t${Version}\\n"}, output, error)) return {};
    return parseInstalledPackages(output);
}

PackageRemovalPlan parseRemovalPlan(const QByteArray &output) {
    PackageRemovalPlan plan;
    for (const QByteArray &line : output.split('\n')) {
        if (line.startsWith("Remv ")) {
            const QByteArray name = line.mid(5).split(' ').first().trimmed();
            if (!name.isEmpty()) {
                plan.removals.append(QString::fromUtf8(name));
                plan.removalDetails.append(QString::fromUtf8(line).trimmed());
            }
        } else if (line.startsWith("Inst ") || line.startsWith("Conf ") || line.startsWith("Purg ")) {
            plan.otherActions.append(QString::fromUtf8(line).trimmed());
        }
    }
    return plan;
}

PackageRemovalPlan simulatePackageRemoval(const QString &package, QString &error) {
    static const QRegularExpression allowed("^[a-z0-9][a-z0-9+.-]*[a-z0-9](?::[a-z0-9][a-z0-9-]*)?$");
    error.clear();
    if (!allowed.match(package).hasMatch()) {
        error = "Invalid package name";
        return {};
    }
    QString inventoryError;
    const auto inventory = installedPackages(inventoryError);
    if (!inventoryError.isEmpty()) {
        error = inventoryError;
        return {};
    }
    bool installed = false;
    for (const auto &entry : inventory) {
        if (entry.name == package) {
            installed = true;
            break;
        }
    }
    if (!installed) {
        error = "Package is not installed";
        return {};
    }
    QByteArray output;
    if (!run("/usr/bin/apt-get", {"-s", "remove", "--", package}, output, error, 30000)) return {};
    auto plan = parseRemovalPlan(output);
    if (!plan.removals.contains(package)) {
        error = "APT did not include the selected package in its removal plan";
        return {};
    }
    return plan;
}

QString installedPackageVersion(const QVector<PackageEntry> &inventory, const QString &package) {
    for (const auto &entry : inventory)
        if (entry.name == package) return entry.version;
    return {};
}

QString removalPlanFingerprint(const QString &package, const QString &version,
                               const PackageRemovalPlan &plan) {
    QByteArray data;
    const QStringList parts = QStringList{package, version, QString::number(plan.removals.size())}
                              + plan.removals + QStringList{QString::number(plan.removalDetails.size())}
                              + plan.removalDetails + QStringList{QString::number(plan.otherActions.size())}
                              + plan.otherActions;
    for (const QString &part : parts) {
        const QByteArray bytes = part.toUtf8();
        data += QByteArray::number(bytes.size()) + ':' + bytes;
    }
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}
