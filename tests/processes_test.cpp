#include "processes.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cassert>
#include <algorithm>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

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
    const auto live = listProcesses();
    const auto self = std::find_if(live.cbegin(), live.cend(), [](const ProcessEntry &entry) {
        return entry.pid == ::getpid();
    });
    assert(self != live.cend());
    assert(self->uid == ::getuid());
    assert(!self->name.isEmpty() && self->startTicks > 0);
    QString error;
    assert(!terminateOwnProcess(entries.first(), error, root.path()));
    if (getuid() != 0) {
        int ready[2];
        assert(pipe(ready) == 0);
        const pid_t child = fork();
        assert(child >= 0);
        if (child == 0) {
            close(ready[0]);
            const char signal = 'r';
            if (write(ready[1], &signal, 1) != 1) _exit(1);
            close(ready[1]);
            pause();
            _exit(1);
        }
        close(ready[1]);
        char signal = 0;
        assert(read(ready[0], &signal, 1) == 1 && signal == 'r');
        close(ready[0]);
        const auto children = listProcesses();
        const auto childEntry = std::find_if(children.cbegin(), children.cend(),
                                             [child](const ProcessEntry &process) {
                                                 return process.pid == child;
                                             });
        if (childEntry == children.cend()) {
            kill(child, SIGKILL);
            waitpid(child, nullptr, 0);
            assert(false && "child process was not listed");
        }
        auto stale = *childEntry;
        ++stale.startTicks;
        const bool staleRejected = !terminateOwnProcess(stale, error);
        if (!staleRejected || kill(child, 0) != 0) {
            kill(child, SIGKILL);
            waitpid(child, nullptr, 0);
            assert(false && "stale process identity was accepted");
        }
        const bool sent = terminateOwnProcess(*childEntry, error);
        if (!sent) kill(child, SIGKILL);
        int status = 0;
        assert(waitpid(child, &status, 0) == child);
        assert(sent && WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);
    }
    return 0;
}
