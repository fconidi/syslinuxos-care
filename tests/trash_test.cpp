#include "trash.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cassert>
#include <sys/stat.h>
#include <unistd.h>

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

    const QByteArray uid = QByteArray::number(::geteuid());
    const QString uidText = QString::fromLatin1(uid);
    assert(root.mkpath("mount-a/.Trash/" + uidText + "/files"));
    assert(root.mkpath("mount-a/.Trash/" + uidText + "/info"));
    assert(root.mkpath("mount-a/.Trash-" + uidText + "/files"));
    assert(root.mkpath("mount-a/.Trash-" + uidText + "/info"));
    assert(::chmod(QFile::encodeName(root.filePath("mount-a/.Trash")).constData(), 01777) == 0);
    write(root.filePath("mount-a/.Trash/" + uidText + "/files/shared"), "shared");
    write(root.filePath("mount-a/.Trash/" + uidText + "/info/shared.trashinfo"),
          "[Trash Info]\nPath=documents/shared\nDeletionDate=2026-10-23T20:00:00\n");
    write(root.filePath("mount-a/.Trash-" + uidText + "/files/private"), "private");
    write(root.filePath("mount-a/.Trash-" + uidText + "/info/private.trashinfo"),
          "[Trash Info]\nPath=/absolute/private\nDeletionDate=2026-10-23T20:00:00\n");
    assert(root.mkpath("mount-b/.Trash/" + uidText + "/files"));
    assert(root.mkpath("mount-b/.Trash/" + uidText + "/info"));
    write(root.filePath("mount-b/.Trash/" + uidText + "/files/ignored"), "ignored");
    write(root.filePath("mount-b/.Trash/" + uidText + "/info/ignored.trashinfo"),
          "[Trash Info]\nPath=/ignored\nDeletionDate=2026-10-23T20:00:00\n");
    assert(root.mkpath("mount-b/.Trash-" + uidText + "/files"));
    assert(root.mkpath("mount-b/.Trash-" + uidText + "/info"));
    write(root.filePath("mount-b/.Trash-" + uidText + "/files/fallback"), "fallback");
    write(root.filePath("mount-b/.Trash-" + uidText + "/info/fallback.trashinfo"),
          "[Trash Info]\nPath=/fallback\nDeletionDate=2026-10-23T20:00:00\n");
    assert(root.mkpath("mount-c"));
    assert(QFile::link(root.filePath("mount-a/.Trash-" + uidText),
                       root.filePath("mount-c/.Trash-" + uidText)));
    const auto locations = scanTrashLocations(root.filePath("data"),
        {root.filePath("mount-a"), root.filePath("mount-b"), root.filePath("mount-c"),
         root.filePath("mount-a")}, ::geteuid());
    assert(locations.size() == 4); // home, shared mount, private mount, fallback mount
    assert(locations[1].entries.size() == 1);
    assert(locations[1].entries[0].originalPath == root.filePath("mount-a/documents/shared"));
    assert(locations[2].entries[0].originalPath == "/absolute/private");
    assert(locations[3].entries[0].id == "fallback");
    assert(removeTrashEntry(locations[1], locations[1].entries[0], error));
    assert(!QFile::exists(root.filePath("mount-a/.Trash/" + uidText + "/files/shared")));
    return 0;
}
