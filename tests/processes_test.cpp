#include "processes.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cassert>

int main() {
    QTemporaryDir temporary(QDir::tempPath() + "/processes-test-XXXXXX");
    assert(temporary.isValid());
    QDir root(temporary.path());
    assert(root.mkpath("123"));
    QFile status(root.filePath("123/status"));
    assert(status.open(QIODevice::WriteOnly));
    assert(status.write("Name:\texample\nUid:\t1000\t1000\t1000\t1000\nVmRSS:\t42 kB\n") > 0);
    status.close();
    QFile stat(root.filePath("123/stat"));
    assert(stat.open(QIODevice::WriteOnly));
    assert(stat.write("123 (example) S 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 987 0 0\n") > 0);
    stat.close();
    const auto entries = listProcesses(root.path());
    assert(entries.size() == 1);
    assert(entries.first().pid == 123);
    assert(entries.first().name == "example");
    assert(entries.first().residentBytes == 42 * 1024);
    assert(entries.first().startTicks == 987);
    QString error;
    assert(!terminateOwnProcess(entries.first(), error, root.path()));
    return 0;
}
