#include "trash.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cassert>

static void write(const QString &path, const QByteArray &content) {
    QFile file(path);
    assert(file.open(QIODevice::WriteOnly));
    assert(file.write(content) == content.size());
}

int main() {
    QTemporaryDir temporary(QDir::tempPath() + "/trash-test-XXXXXX");
    assert(temporary.isValid());
    QDir root(temporary.path());
    const TrashScan empty = scanHomeTrash(root.filePath("missing"));
    assert(empty.error.isEmpty() && empty.entries.isEmpty());
    assert(root.mkpath("data/Trash/files/folder"));
    assert(root.mkpath("data/Trash/info"));
    write(root.filePath("outside"), "keep");
    write(root.filePath("data/Trash/files/document"), "test");
    write(root.filePath("data/Trash/info/document.trashinfo"),
          "[Trash Info]\nPath=/home/user/document\nDeletionDate=2026-10-23T20:00:00\n");
    write(root.filePath("data/Trash/files/folder/inside"), "inside");
    assert(QFile::link(root.filePath("outside"), root.filePath("data/Trash/files/folder/link")));
    write(root.filePath("data/Trash/info/folder.trashinfo"),
          "[Trash Info]\nPath=/home/user/folder\nDeletionDate=2026-10-23T20:00:00\n");
    const TrashScan scan = scanHomeTrash(root.filePath("data"));
    assert(scan.error.isEmpty());
    assert(scan.entries.size() == 2);
    auto document = std::find_if(scan.entries.cbegin(), scan.entries.cend(), [](const auto &item) {
        return item.id == "document";
    });
    assert(document != scan.entries.cend());
    assert(document->size == 4);
    QString error;
    write(root.filePath("data/Trash/files/document"), "changed");
    assert(!removeTrashEntry(scan, *document, error));
    const TrashScan refreshed = scanHomeTrash(root.filePath("data"));
    auto currentDocument = std::find_if(refreshed.entries.cbegin(), refreshed.entries.cend(), [](const auto &item) {
        return item.id == "document";
    });
    assert(currentDocument != refreshed.entries.cend());
    assert(removeTrashEntry(refreshed, *currentDocument, error));
    assert(!QFile::exists(root.filePath("data/Trash/files/document")));
    assert(!QFile::exists(root.filePath("data/Trash/info/document.trashinfo")));
    auto folder = std::find_if(scan.entries.cbegin(), scan.entries.cend(), [](const auto &item) {
        return item.id == "folder";
    });
    assert(folder != scan.entries.cend());
    assert(removeTrashEntry(scan, *folder, error));
    assert(QFile::exists(root.filePath("outside")));
    assert(!QFile::exists(root.filePath("data/Trash/files/folder")));
    return 0;
}
