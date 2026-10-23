#include "packages.h"

#include <QProcess>
#include <QRegularExpression>

namespace {
bool run(const QString &program, const QStringList &args, QByteArray &output, QString &error,
         int timeout = 15000) {
    QProcess process;
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
    if (!run("dpkg-query", {"-W", "-f=${db:Status-Abbrev}\\t${binary:Package}\\t${Version}\\n"}, output, error)) return {};
    return parseInstalledPackages(output);
}

QStringList parseRemovalPlan(const QByteArray &output) {
    QStringList names;
    for (const QByteArray &line : output.split('\n')) {
        if (!line.startsWith("Remv ")) continue;
        const QByteArray name = line.mid(5).split(' ').first().trimmed();
        if (!name.isEmpty()) names.append(QString::fromUtf8(name));
    }
    return names;
}

QStringList simulatePackageRemoval(const QString &package, QString &error) {
    static const QRegularExpression allowed("^[a-z0-9][a-z0-9+.-]*(?::[a-z0-9-]+)?$");
    error.clear();
    if (!allowed.match(package).hasMatch()) {
        error = "Invalid package name";
        return {};
    }
    QByteArray output;
    if (!run("apt-get", {"-s", "remove", "--", package}, output, error, 30000)) return {};
    return parseRemovalPlan(output);
}
