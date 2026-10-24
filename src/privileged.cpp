#include "packages.h"

#include <QCoreApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTextStream>

#include <unistd.h>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    const QStringList args = app.arguments();
    static const QRegularExpression hex("^[a-f0-9]{64}$");
    if (geteuid() != 0 || args.size() != 5 || args[1] != "remove-package" ||
        args[2].isEmpty() || args[3].isEmpty() || !hex.match(args[4]).hasMatch()) {
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
