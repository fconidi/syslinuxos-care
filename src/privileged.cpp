#include "packages.h"
#include "services.h"
#include "aptcache.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTextStream>

#include <cerrno>
#include <unistd.h>

namespace {
int removePackage(const QStringList &args, QTextStream &err) {
    static const QRegularExpression hex("^[a-f0-9]{64}$");
    if (args.size() != 5 || args[2].isEmpty() || args[3].isEmpty() ||
        !hex.match(args[4]).hasMatch()) {
        err << "Invalid or unauthorized request\n";
        return 2;
    }

    QString error;
    const auto inventory = installedPackages(error);
    const QString version = installedPackageVersion(inventory, args[2]);
    if (!error.isEmpty() || version.isEmpty() || version != args[3]) {
        err << "Package inventory changed; refresh the preview\n";
        return 3;
    }
    const auto plan = simulatePackageRemoval(args[2], error);
    if (!error.isEmpty() || plan.removals.isEmpty() ||
        removalPlanFingerprint(args[2], version, plan) != args[4]) {
        err << "APT removal plan changed; refresh the preview\n";
        return 3;
    }

    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("LC_ALL", "C");
    environment.insert("DEBIAN_FRONTEND", "noninteractive");
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start("/usr/bin/apt-get", {"-y", "remove", "--", args[2]});
    if (!process.waitForStarted(3000)) {
        err << "Could not start APT\n";
        return 4;
    }
    if (!process.waitForFinished(900000)) {
        process.kill();
        process.waitForFinished();
        err << "APT timed out; inspect the package state before retrying\n";
        return 4;
    }
    QTextStream out(stdout);
    out << QString::fromUtf8(process.readAll());
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        err << "APT failed; the package state may have changed partially\n";
        return 4;
    }
    return 0;
}

QString systemServiceState(const QString &unit, QString &error) {
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("LC_ALL", "C");
    process.setProcessEnvironment(environment);
    process.start("/usr/bin/systemctl", {"--system", "is-enabled", unit});
    if (!process.waitForStarted(3000) || !process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished();
        error = "Could not read service startup state";
        return {};
    }
    const QString state = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (process.exitStatus() != QProcess::NormalExit ||
        (state != "enabled" && state != "disabled")) {
        error = "Service startup state is unavailable or unsupported";
        return {};
    }
    return state;
}

int setSystemServiceEnabled(const QStringList &args, QTextStream &err) {
    if (args.size() != 5 || !validServiceUnit(args[2]) ||
        (args[3] != "enabled" && args[3] != "disabled") ||
        (args[4] != "enabled" && args[4] != "disabled") || args[3] == args[4]) {
        err << "Invalid service request\n";
        return 2;
    }
    QString error;
    if (systemServiceState(args[2], error) != args[3]) {
        err << "Service startup state changed; refresh the preview\n";
        return 3;
    }
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("LC_ALL", "C");
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start("/usr/bin/systemctl", {"--system",
                                           args[4] == "enabled" ? "enable" : "disable",
                                           args[2]});
    if (!process.waitForStarted(3000) || !process.waitForFinished(30000)) {
        process.kill();
        process.waitForFinished();
        err << "Service change timed out; inspect its state before retrying\n";
        return 4;
    }
    QTextStream out(stdout);
    out << QString::fromUtf8(process.readAll());
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 ||
        systemServiceState(args[2], error) != args[4]) {
        err << "Service change failed or was partial; inspect its state before retrying\n";
        return 4;
    }
    return 0;
}

int cleanAptArchives(const QStringList &args, QTextStream &err) {
    if (args.size() != 3) {
        err << "Invalid APT cache request\n";
        return 2;
    }
    QByteArray request;
    char buffer[4096];
    for (;;) {
        const ssize_t size = ::read(STDIN_FILENO, buffer, sizeof(buffer));
        if (size == 0) break;
        if (size < 0) {
            if (errno == EINTR) continue;
            err << "Could not read the selected archives\n";
            return 2;
        }
        if (request.size() + size > 1024 * 1024) {
            err << "Archive selection is too large\n";
            return 2;
        }
        request.append(buffer, size);
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(request, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        err << "Invalid archive selection\n";
        return 2;
    }
    QStringList selected;
    for (const auto &value : document.array()) {
        if (!value.isString()) {
            err << "Invalid archive selection\n";
            return 2;
        }
        selected << value.toString();
    }
    QStringList outcomes;
    const bool succeeded = removeAptArchives(aptArchiveDirectory(), selected, args[2], outcomes);
    QTextStream out(stdout);
    out << outcomes.join('\n') << '\n';
    if (!succeeded) {
        err << "APT archive cleanup was refused or incomplete\n";
        return 4;
    }
    return 0;
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    const QStringList args = app.arguments();
    if (geteuid() != 0 || args.size() < 2) {
        err << "Invalid or unauthorized request\n";
        return 2;
    }
    if (args[1] == "remove-package") return removePackage(args, err);
    if (args[1] == "set-service-enabled") return setSystemServiceEnabled(args, err);
    if (args[1] == "clean-apt-cache") return cleanAptArchives(args, err);
    err << "Unsupported request\n";
    return 2;
}
