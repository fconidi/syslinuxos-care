#include "autostart.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cassert>

int main() {
    QTemporaryDir temporary(QDir::tempPath() + "/autostart-test-XXXXXX");
    assert(temporary.isValid());
    QDir root(temporary.path());
    assert(root.mkpath("system/autostart"));
    assert(root.mkpath("user/autostart"));
    QFile system(root.filePath("system/autostart/example.desktop"));
    assert(system.open(QIODevice::WriteOnly));
    assert(system.write("[Desktop Entry]\nName=System example\nExec=example\n") > 0);
    system.close();
    QFile user(root.filePath("user/autostart/example.desktop"));
    assert(user.open(QIODevice::WriteOnly));
    assert(user.write("[Desktop Entry]\nName=User override\nHidden=true\n") > 0);
    user.close();
    const auto entries = listAutostart(root.filePath("user"), {root.filePath("system")});
    assert(entries.size() == 1);
    assert(entries.first().name == "User override");
    assert(!entries.first().enabled);
    assert(entries.first().userEntry);
    QString error;
    assert(user.open(QIODevice::WriteOnly | QIODevice::Truncate));
    assert(user.write("[Desktop Entry]\nName=Changed after preview\nHidden=true\n") > 0);
    user.close();
    assert(!setAutostartEnabled(entries.first(), true, root.filePath("user"),
                                {root.filePath("system")}, error));
    assert(error.contains("changed", Qt::CaseInsensitive));
    assert(user.open(QIODevice::ReadOnly));
    assert(user.readAll().contains("Changed after preview"));
    user.close();
    assert(user.open(QIODevice::WriteOnly | QIODevice::Truncate));
    assert(user.write("[Desktop Entry]\nName=User override\nHidden=true\n") > 0);
    user.close();
    assert(setAutostartEnabled(entries.first(), true, root.filePath("user"),
                               {root.filePath("system")}, error));
    auto updated = listAutostart(root.filePath("user"), {root.filePath("system")});
    assert(updated.first().enabled);
    assert(setAutostartEnabled(updated.first(), false, root.filePath("user"),
                               {root.filePath("system")}, error));
    updated = listAutostart(root.filePath("user"), {root.filePath("system")});
    assert(!updated.first().enabled);
    assert(QFile::remove(user.fileName()));
    updated = listAutostart(root.filePath("user"), {root.filePath("system")});
    assert(updated.first().enabled && !updated.first().userEntry);
    assert(setAutostartEnabled(updated.first(), false, root.filePath("user"),
                               {root.filePath("system")}, error));
    updated = listAutostart(root.filePath("user"), {root.filePath("system")});
    assert(!updated.first().enabled && updated.first().userEntry);
    assert(setAutostartEnabled(updated.first(), true, root.filePath("user"),
                               {root.filePath("system")}, error));
    updated = listAutostart(root.filePath("user"), {root.filePath("system")});
    assert(updated.first().enabled && !updated.first().userEntry);
    return 0;
}
