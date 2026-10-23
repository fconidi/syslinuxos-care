#include "metrics.h"
#include "scanner.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMainWindow>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QMainWindow window;
    window.setWindowTitle(QObject::tr("System maintenance"));
    auto *tabs = new QTabWidget(&window);
    auto *page = new QWidget(tabs);
    auto *outer = new QVBoxLayout(page);
    auto *title = new QLabel(QObject::tr("System overview"), page);
    QFont titleFont = title->font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    title->setFont(titleFont);
    outer->addWidget(title);
    auto *form = new QFormLayout;
    auto *cpu = new QLabel(page);
    auto *memory = new QLabel(page);
    auto *disk = new QLabel(page);
    auto *network = new QLabel(page);
    form->addRow(QObject::tr("CPU"), cpu);
    form->addRow(QObject::tr("Memory"), memory);
    form->addRow(QObject::tr("Root disk"), disk);
    form->addRow(QObject::tr("Network"), network);
    outer->addLayout(form);
    auto *refresh = new QPushButton(QObject::tr("Refresh now"), page);
    outer->addWidget(refresh);
    outer->addStretch();
    tabs->addTab(page, QObject::tr("Overview"));

    auto *cleanPage = new QWidget(tabs);
    auto *cleanLayout = new QVBoxLayout(cleanPage);
    cleanLayout->addWidget(new QLabel(QObject::tr("Thumbnail cache"), cleanPage));
    cleanLayout->addWidget(new QLabel(QObject::tr("Review each file before removing it. All items start unselected."), cleanPage));
    auto *scanButton = new QPushButton(QObject::tr("Scan thumbnails"), cleanPage);
    auto *cleanButton = new QPushButton(QObject::tr("Remove selected"), cleanPage);
    cleanButton->setEnabled(false);
    auto *scanStatus = new QLabel(cleanPage);
    auto *table = new QTableWidget(0, 2, cleanPage);
    table->setHorizontalHeaderLabels({QObject::tr("File"), QObject::tr("Size")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    cleanLayout->addWidget(scanButton);
    cleanLayout->addWidget(scanStatus);
    cleanLayout->addWidget(table);
    cleanLayout->addWidget(cleanButton);
    tabs->addTab(cleanPage, QObject::tr("Cleanup"));

    auto *diskPage = new QWidget(tabs);
    auto *diskLayout = new QVBoxLayout(diskPage);
    auto *diskPath = new QLabel(QDir::homePath(), diskPage);
    diskPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *chooseFolder = new QPushButton(QObject::tr("Choose folder"), diskPage);
    auto *analyze = new QPushButton(QObject::tr("Analyze space"), diskPage);
    auto *diskStatus = new QLabel(diskPage);
    auto *diskTable = new QTableWidget(0, 2, diskPage);
    diskTable->setHorizontalHeaderLabels({QObject::tr("Folder"), QObject::tr("Data size")});
    diskTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    diskTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    diskLayout->addWidget(diskPath);
    diskLayout->addWidget(chooseFolder);
    diskLayout->addWidget(analyze);
    diskLayout->addWidget(diskStatus);
    diskLayout->addWidget(diskTable);
    tabs->addTab(diskPage, QObject::tr("Disk space"));
    window.setCentralWidget(tabs);
    window.resize(820, 560);

    MetricsReader reader;
    auto update = [&] {
        const Metrics m = reader.sample();
        cpu->setText(m.cpuPercent < 0 ? QObject::tr("Collecting…")
                                       : QString::number(m.cpuPercent, 'f', 1) + "%");
        memory->setText(m.memoryTotal ? formatBytes(m.memoryUsed) + " / " + formatBytes(m.memoryTotal)
                                      : QObject::tr("Unavailable"));
        disk->setText(m.diskTotal ? formatBytes(m.diskUsed) + " / " + formatBytes(m.diskTotal)
                                  : QObject::tr("Unavailable"));
        network->setText(QObject::tr("Down: %1/s · Up: %2/s")
                             .arg(formatBytes(m.rxBytesPerSecond), formatBytes(m.txBytesPerSecond)));
    };
    QObject::connect(refresh, &QPushButton::clicked, &window, update);
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &window, update);
    timer.start(2000);
    update();

    ScanResult latest;
    const auto cacheHome = qEnvironmentVariable("XDG_CACHE_HOME");
    const QString cachePath = QDir::isAbsolutePath(cacheHome) ? cacheHome : QDir::homePath() + "/.cache";
    const QString thumbnailPath = QDir(cachePath).filePath("thumbnails");
    auto scan = [&] {
        table->setRowCount(0);
        latest = scanRegularFiles(thumbnailPath, 5000);
        if (!latest.error.isEmpty()) {
            scanStatus->setText(QObject::tr("Scan unavailable: %1").arg(latest.error));
            cleanButton->setEnabled(false);
            return;
        }
        quint64 bytes = 0;
        table->setRowCount(latest.items.size());
        for (int row = 0; row < latest.items.size(); ++row) {
            const auto &item = latest.items[row];
            auto *name = new QTableWidgetItem(item.relativePath);
            name->setFlags(name->flags() | Qt::ItemIsUserCheckable);
            name->setCheckState(Qt::Unchecked);
            table->setItem(row, 0, name);
            table->setItem(row, 1, new QTableWidgetItem(formatBytes(item.size)));
            bytes += item.size;
        }
        scanStatus->setText(QObject::tr("%1 files · %2%3")
                                .arg(latest.items.size()).arg(formatBytes(bytes),
                                     latest.truncated ? QObject::tr(" · scan limit reached") : QString{}));
        cleanButton->setEnabled(!latest.items.isEmpty());
    };
    QObject::connect(scanButton, &QPushButton::clicked, &window, scan);
    QObject::connect(cleanButton, &QPushButton::clicked, &window, [&] {
        QVector<int> selected;
        quint64 bytes = 0;
        for (int row = 0; row < table->rowCount(); ++row) {
            if (table->item(row, 0)->checkState() == Qt::Checked) {
                selected.append(row);
                bytes += latest.items[row].size;
            }
        }
        if (selected.isEmpty()) return;
        const auto message = QObject::tr("Permanently remove %1 selected thumbnail files (%2)?")
                                 .arg(selected.size()).arg(formatBytes(bytes));
        if (QMessageBox::question(&window, QObject::tr("Confirm cleanup"), message,
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        int removed = 0;
        QString firstError;
        for (int row : selected) {
            QString error;
            if (removeScannedFile(latest, latest.items[row], error)) ++removed;
            else if (firstError.isEmpty()) firstError = error;
        }
        scan();
        QMessageBox::information(&window, QObject::tr("Cleanup result"),
            QObject::tr("Removed %1 of %2 files. %3")
                .arg(removed).arg(selected.size()).arg(firstError));
    });
    QObject::connect(chooseFolder, &QPushButton::clicked, &window, [&] {
        const QString chosen = QFileDialog::getExistingDirectory(&window, QObject::tr("Choose a folder to analyze"),
                                                                  diskPath->text());
        if (!chosen.isEmpty()) diskPath->setText(chosen);
    });
    QObject::connect(analyze, &QPushButton::clicked, &window, [&] {
        diskTable->setRowCount(0);
        const ScanResult result = scanRegularFiles(diskPath->text(), 50000);
        if (!result.error.isEmpty()) {
            diskStatus->setText(QObject::tr("Analysis unavailable: %1").arg(result.error));
            return;
        }
        QMap<QString, quint64> sizes;
        quint64 totalBytes = 0;
        for (const auto &item : result.items) {
            const int slash = item.relativePath.indexOf('/');
            const QString group = slash < 0 ? QObject::tr("Files in this folder")
                                             : item.relativePath.left(slash);
            sizes[group] += item.size;
            totalBytes += item.size;
        }
        QVector<QPair<QString, quint64>> groups;
        for (auto it = sizes.cbegin(); it != sizes.cend(); ++it) groups.append({it.key(), it.value()});
        std::sort(groups.begin(), groups.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
        diskTable->setRowCount(groups.size());
        for (int row = 0; row < groups.size(); ++row) {
            diskTable->setItem(row, 0, new QTableWidgetItem(groups[row].first));
            diskTable->setItem(row, 1, new QTableWidgetItem(formatBytes(groups[row].second)));
        }
        diskStatus->setText(QObject::tr("%1 files · %2 of data%3")
                                .arg(result.items.size()).arg(formatBytes(totalBytes),
                                    result.truncated ? QObject::tr(" · scan limit reached") : QString{}));
    });
    window.show();
    return app.exec();
}
