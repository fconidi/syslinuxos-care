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
    return 0;
}
