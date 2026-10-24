#include "scanner.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cassert>

static void write(const QString &path, const QByteArray &data) {
    QFile file(path);
    assert(file.open(QIODevice::WriteOnly));
    assert(file.write(data) == data.size());
}

int main() {
    QTemporaryDir temporary(QDir::tempPath() + "/scanner-test-XXXXXX");
    assert(temporary.isValid());
    QDir root(temporary.path());
    assert(root.mkdir("cache"));
    assert(root.mkdir("other"));
    write(root.filePath("cache/a"), "a");
    write(root.filePath("cache/b"), "bb");
    write(root.filePath("other/keep"), "safe");
    assert(QFile::link(root.filePath("other/keep"), root.filePath("cache/link")));
    assert(QFile::link(root.filePath("other"), root.filePath("cache/linked-dir")));
    const ScanResult scan = scanRegularFiles(root.filePath("cache"));
    assert(scan.error.isEmpty());
    assert(scan.items.size() == 2);
    assert(!scan.truncated);
    std::atomic_bool cancel{true};
    const ScanResult cancelled = scanRegularFiles(root.filePath("cache"), 50000, &cancel);
    assert(cancelled.cancelled);
    assert(cancelled.items.isEmpty());
    QString error;
    auto a = std::find_if(scan.items.begin(), scan.items.end(), [](const ScanItem &item) {
        return item.relativePath == "a";
    });
    assert(a != scan.items.end());
    assert(removeScannedFile(scan, *a, error));
    const QStringList remaining = QDir(root.filePath("cache")).entryList(QDir::Files | QDir::Hidden);
    assert(std::none_of(remaining.cbegin(), remaining.cend(),
                        [](const QString &name) { return name.startsWith(".syslinuxos-care-"); }));
    assert(QFile::exists(root.filePath("other/keep")));
    const ScanResult second = scanRegularFiles(root.filePath("cache"));
    assert(second.items.size() == 1);
    assert(QFile::rename(root.filePath("cache/b"), root.filePath("other/original-b")));
    write(root.filePath("cache/b"), "bb");
    assert(!removeScannedFile(second, second.items.first(), error));
    assert(QFile::exists(root.filePath("cache/b")));
    assert(QFile::exists(root.filePath("other/original-b")));
    return 0;
}
