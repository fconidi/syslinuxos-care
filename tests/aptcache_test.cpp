#include "aptcache.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cassert>

static void writeFile(const QString &path, const QByteArray &data) {
    QFile file(path);
    assert(file.open(QIODevice::WriteOnly));
    assert(file.write(data) == data.size());
}

int main() {
    QTemporaryDir temporary(QDir::tempPath() + "/aptcache-test-XXXXXX");
    assert(temporary.isValid());
    QDir root(temporary.path());
    writeFile(root.filePath("lock"), {});
    writeFile(root.filePath("alpha_1.0_amd64.deb"), "alpha");
    writeFile(root.filePath("beta_2.0_all.deb"), "beta");
    writeFile(root.filePath("notes.txt"), "keep");
    assert(root.mkdir("partial"));
    writeFile(root.filePath("partial/incomplete.deb"), "keep");
    assert(QFile::link(root.filePath("notes.txt"), root.filePath("linked.deb")));

    const auto scan = scanAptArchives(temporary.path());
    assert(scan.error.isEmpty());
    assert(scan.items.size() == 2);
    std::atomic_bool cancel{true};
    assert(scanAptArchives(temporary.path(), &cancel).cancelled);
    assert(scan.items.first().relativePath == "alpha_1.0_amd64.deb");
    assert(validAptArchiveName("alpha_1.0_amd64.deb"));
    assert(!validAptArchiveName("../other.deb"));
    assert(!validAptArchiveName("partial/file.deb"));
    QStringList outcomes;
    const QString fingerprint = aptArchiveFingerprint(scan);
    assert(!removeAptArchives(temporary.path(), {"../other.deb"}, fingerprint, outcomes));
    assert(!removeAptArchives(temporary.path(), {"alpha_1.0_amd64.deb", "alpha_1.0_amd64.deb"},
                              fingerprint, outcomes));
    writeFile(root.filePath("gamma_3.0_all.deb"), "changed");
    assert(!removeAptArchives(temporary.path(), {"alpha_1.0_amd64.deb"}, fingerprint, outcomes));
    assert(QFile::exists(root.filePath("alpha_1.0_amd64.deb")));

    const auto current = scanAptArchives(temporary.path());
    assert(removeAptArchives(temporary.path(), {"alpha_1.0_amd64.deb"},
                             aptArchiveFingerprint(current), outcomes));
    assert(outcomes.size() == 1);
    assert(!QFile::exists(root.filePath("alpha_1.0_amd64.deb")));
    assert(QFile::exists(root.filePath("beta_2.0_all.deb")));
    assert(QFile::exists(root.filePath("partial/incomplete.deb")));
    assert(QFile::exists(root.filePath("notes.txt")));
    return 0;
}
