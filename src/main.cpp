#include "metrics.h"
#include "scanner.h"
#include "autostart.h"
#include "processes.h"
#include "services.h"
#include "packages.h"
#include "trash.h"
#include "browsercache.h"
#include "historychart.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMainWindow>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QProgressBar>
#include <QProcess>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QTranslator>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <memory>
#include <unistd.h>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("SysLinuxOS Care");
    QTranslator translator;
    if (translator.load(QLocale(), "syslinuxos-care", "_", ":/i18n"))
        app.installTranslator(&translator);
    QMainWindow window;
    window.setWindowTitle(QObject::tr("SysLinuxOS Care"));
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
    auto *cpuBar = new QProgressBar(page);
    cpuBar->setRange(0, 100);
    cpuBar->setFormat(QObject::tr("CPU: %p%"));
    auto *memoryBar = new QProgressBar(page);
    memoryBar->setRange(0, 100);
    memoryBar->setFormat(QObject::tr("Memory: %p%"));
    outer->addWidget(cpuBar);
    outer->addWidget(memoryBar);
    auto *history = new HistoryChart(page);
    history->setAccessibleName(QObject::tr("CPU and memory usage history"));
    outer->addWidget(history);
    auto *controls = new QHBoxLayout;
    auto *pauseMonitoring = new QPushButton(QObject::tr("Pause monitoring"), page);
    pauseMonitoring->setCheckable(true);
    auto *interval = new QComboBox(page);
    interval->addItem(QObject::tr("2 seconds"), 2000);
    interval->addItem(QObject::tr("5 seconds"), 5000);
    interval->addItem(QObject::tr("10 seconds"), 10000);
    controls->addWidget(pauseMonitoring);
    controls->addWidget(new QLabel(QObject::tr("Update interval"), page));
    controls->addWidget(interval);
    outer->addLayout(controls);
    auto *refresh = new QPushButton(QObject::tr("Refresh now"), page);
    outer->addWidget(refresh);
    outer->addStretch();
    tabs->addTab(page, QObject::tr("Overview"));

    auto *cleanPage = new QWidget(tabs);
    auto *cleanLayout = new QVBoxLayout(cleanPage);
    cleanLayout->addWidget(new QLabel(QObject::tr("Select a cache category"), cleanPage));
    cleanLayout->addWidget(new QLabel(QObject::tr("Review each file before removing it. All items start unselected."), cleanPage));
    auto *cleanCategory = new QComboBox(cleanPage);
    cleanCategory->addItem(QObject::tr("Thumbnail cache"));
    cleanCategory->addItem(QObject::tr("Font cache"));
    auto *scanButton = new QPushButton(QObject::tr("Scan selected cache"), cleanPage);
    auto *cleanButton = new QPushButton(QObject::tr("Remove selected"), cleanPage);
    cleanButton->setEnabled(false);
    auto *scanStatus = new QLabel(cleanPage);
    auto *table = new QTableWidget(0, 2, cleanPage);
    table->setHorizontalHeaderLabels({QObject::tr("File"), QObject::tr("Size")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    cleanLayout->addWidget(cleanCategory);
    cleanLayout->addWidget(scanButton);
    cleanLayout->addWidget(scanStatus);
    cleanLayout->addWidget(table);
    cleanLayout->addWidget(cleanButton);
    tabs->addTab(cleanPage, QObject::tr("Cleanup"));

    auto *trashPage = new QWidget(tabs);
    auto *trashLayout = new QVBoxLayout(trashPage);
    trashLayout->addWidget(new QLabel(QObject::tr("Home and mounted-volume trash · permanent removal"), trashPage));
    trashLayout->addWidget(new QLabel(QObject::tr("Review the original location of each item. Nothing is selected automatically."), trashPage));
    auto *trashScanButton = new QPushButton(QObject::tr("Scan trash"), trashPage);
    auto *trashStatus = new QLabel(trashPage);
    auto *trashTable = new QTableWidget(0, 4, trashPage);
    trashTable->setHorizontalHeaderLabels({QObject::tr("Item"), QObject::tr("Trash location"),
                                          QObject::tr("Original location"), QObject::tr("Size")});
    trashTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    trashTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    auto *trashRemove = new QPushButton(QObject::tr("Permanently remove selected"), trashPage);
    trashRemove->setEnabled(false);
    trashLayout->addWidget(trashScanButton);
    trashLayout->addWidget(trashStatus);
    trashLayout->addWidget(trashTable);
    trashLayout->addWidget(trashRemove);
    tabs->addTab(trashPage, QObject::tr("Trash"));

    auto *diskPage = new QWidget(tabs);
    auto *diskLayout = new QVBoxLayout(diskPage);
    auto *diskPath = new QLabel(QDir::homePath(), diskPage);
    diskPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *chooseFolder = new QPushButton(QObject::tr("Choose folder"), diskPage);
    auto *analyze = new QPushButton(QObject::tr("Analyze space"), diskPage);
    auto *cancelAnalysis = new QPushButton(QObject::tr("Cancel analysis"), diskPage);
    cancelAnalysis->setEnabled(false);
    auto *diskStatus = new QLabel(diskPage);
    auto *diskTable = new QTableWidget(0, 2, diskPage);
    diskTable->setHorizontalHeaderLabels({QObject::tr("Folder"), QObject::tr("Data size")});
    diskTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    diskTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    diskLayout->addWidget(diskPath);
    diskLayout->addWidget(chooseFolder);
    diskLayout->addWidget(analyze);
    diskLayout->addWidget(cancelAnalysis);
    diskLayout->addWidget(diskStatus);
    diskLayout->addWidget(diskTable);
    tabs->addTab(diskPage, QObject::tr("Disk space"));

    auto *startupPage = new QWidget(tabs);
    auto *startupLayout = new QVBoxLayout(startupPage);
    startupLayout->addWidget(new QLabel(QObject::tr("Applications configured to start with your session"), startupPage));
    auto *startupRefresh = new QPushButton(QObject::tr("Refresh startup applications"), startupPage);
    auto *startupToggle = new QPushButton(QObject::tr("Enable or disable selected"), startupPage);
    startupToggle->setEnabled(false);
    auto *startupTable = new QTableWidget(0, 3, startupPage);
    startupTable->setHorizontalHeaderLabels({QObject::tr("Application"), QObject::tr("Status"), QObject::tr("Source")});
    startupTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    startupTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    startupTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    startupTable->setSelectionMode(QAbstractItemView::SingleSelection);
    startupLayout->addWidget(startupRefresh);
    startupLayout->addWidget(startupTable);
    startupLayout->addWidget(startupToggle);
    tabs->addTab(startupPage, QObject::tr("Startup"));

    auto *processPage = new QWidget(tabs);
    auto *processLayout = new QVBoxLayout(processPage);
    processLayout->addWidget(new QLabel(QObject::tr("Running processes"), processPage));
    auto *processRefresh = new QPushButton(QObject::tr("Refresh processes"), processPage);
    auto *processTable = new QTableWidget(0, 4, processPage);
    processTable->setHorizontalHeaderLabels({QObject::tr("PID"), QObject::tr("Name"), QObject::tr("UID"), QObject::tr("Memory")});
    processTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    processTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    processTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    processTable->setSelectionMode(QAbstractItemView::SingleSelection);
    auto *processTerminate = new QPushButton(QObject::tr("Request termination"), processPage);
    processTerminate->setEnabled(false);
    processLayout->addWidget(processRefresh);
    processLayout->addWidget(processTable);
    processLayout->addWidget(processTerminate);
    tabs->addTab(processPage, QObject::tr("Processes"));

    auto *servicePage = new QWidget(tabs);
    auto *serviceLayout = new QVBoxLayout(servicePage);
    auto *serviceScope = new QComboBox(servicePage);
    serviceScope->addItem(QObject::tr("User services"));
    serviceScope->addItem(QObject::tr("System services (view only)"));
    auto *serviceRefresh = new QPushButton(QObject::tr("Refresh services"), servicePage);
    auto *serviceStatus = new QLabel(servicePage);
    auto *serviceTable = new QTableWidget(0, 4, servicePage);
    serviceTable->setHorizontalHeaderLabels({QObject::tr("Service"), QObject::tr("State"), QObject::tr("Startup"), QObject::tr("Description")});
    serviceTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    serviceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    serviceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    serviceTable->setSelectionMode(QAbstractItemView::SingleSelection);
    auto *serviceToggle = new QPushButton(QObject::tr("Start or stop selected"), servicePage);
    serviceToggle->setEnabled(false);
    auto *serviceEnable = new QPushButton(QObject::tr("Enable or disable at startup"), servicePage);
    serviceEnable->setEnabled(false);
    serviceLayout->addWidget(serviceScope);
    serviceLayout->addWidget(serviceRefresh);
    serviceLayout->addWidget(serviceStatus);
    serviceLayout->addWidget(serviceTable);
    serviceLayout->addWidget(serviceToggle);
    serviceLayout->addWidget(serviceEnable);
    tabs->addTab(servicePage, QObject::tr("Services"));

    auto *packagePage = new QWidget(tabs);
    auto *packageLayout = new QVBoxLayout(packagePage);
    auto *packageSearch = new QLineEdit(packagePage);
    packageSearch->setPlaceholderText(QObject::tr("Filter installed packages"));
    auto *packageRefresh = new QPushButton(QObject::tr("Refresh packages"), packagePage);
    auto *packageStatus = new QLabel(packagePage);
    auto *packageTable = new QTableWidget(0, 2, packagePage);
    packageTable->setHorizontalHeaderLabels({QObject::tr("Package"), QObject::tr("Version")});
    packageTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    packageTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    packageTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    packageTable->setSelectionMode(QAbstractItemView::SingleSelection);
    auto *simulateRemoval = new QPushButton(QObject::tr("Preview removal"), packagePage);
    auto *removePackage = new QPushButton(QObject::tr("Remove reviewed package"), packagePage);
    removePackage->setEnabled(false);
    simulateRemoval->setEnabled(false);
    auto *removalPlan = new QTextEdit(packagePage);
    removalPlan->setReadOnly(true);
    removalPlan->setPlaceholderText(QObject::tr("Select a package to see what APT would remove."));
    packageLayout->addWidget(packageSearch);
    packageLayout->addWidget(packageRefresh);
    packageLayout->addWidget(packageStatus);
    packageLayout->addWidget(packageTable);
    packageLayout->addWidget(simulateRemoval);
    packageLayout->addWidget(removePackage);
    packageLayout->addWidget(removalPlan);
    tabs->addTab(packagePage, QObject::tr("Packages"));
    window.setCentralWidget(tabs);
    window.resize(820, 560);

    MetricsReader reader;
    auto update = [&] {
        const Metrics m = reader.sample();
        cpu->setText(m.cpuPercent < 0 ? QObject::tr("Collecting…")
                                       : QString::number(m.cpuPercent, 'f', 1) + "%");
        if (m.cpuPercent >= 0) cpuBar->setValue(qRound(m.cpuPercent));
        memory->setText(m.memoryTotal ? formatBytes(m.memoryUsed) + " / " + formatBytes(m.memoryTotal)
                                      : QObject::tr("Unavailable"));
        if (m.memoryTotal) memoryBar->setValue(qRound(100.0 * m.memoryUsed / m.memoryTotal));
        disk->setText(m.diskTotal ? formatBytes(m.diskUsed) + " / " + formatBytes(m.diskTotal)
                                  : QObject::tr("Unavailable"));
        network->setText(QObject::tr("Down: %1/s · Up: %2/s")
                             .arg(formatBytes(m.rxBytesPerSecond), formatBytes(m.txBytesPerSecond)));
        if (m.cpuPercent >= 0 && m.memoryTotal)
            history->appendSample(m.cpuPercent, 100.0 * m.memoryUsed / m.memoryTotal);
    };
    QObject::connect(refresh, &QPushButton::clicked, &window, update);
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &window, [&] {
        if (!pauseMonitoring->isChecked()) update();
    });
    QObject::connect(pauseMonitoring, &QPushButton::toggled, &window, [&](bool paused) {
        pauseMonitoring->setText(paused ? QObject::tr("Resume monitoring") : QObject::tr("Pause monitoring"));
    });
    QObject::connect(interval, &QComboBox::currentIndexChanged, &window, [&] {
        timer.setInterval(interval->currentData().toInt());
    });
    timer.start(2000);
    update();

    ScanResult latest;
    const auto cacheHome = qEnvironmentVariable("XDG_CACHE_HOME");
    const QString cachePath = QDir::isAbsolutePath(cacheHome) ? cacheHome : QDir::homePath() + "/.cache";
    cleanCategory->setItemData(0, QDir(cachePath).filePath("thumbnails"));
    cleanCategory->setItemData(1, QDir(cachePath).filePath("fontconfig"));
    const auto browserCaches = discoverBrowserCaches(cachePath);
    for (const auto &browser : browserCaches)
        cleanCategory->addItem(QObject::tr("Browser cache: %1 (%2)")
                                   .arg(browserName(browser.kind), browser.profile), browser.path);
    auto selectedBrowser = [&]() -> const BrowserCache * {
        const int index = cleanCategory->currentIndex() - 2;
        return index >= 0 && index < browserCaches.size() ? &browserCaches[index] : nullptr;
    };
    auto scan = [&] {
        table->setRowCount(0);
        if (const auto *browser = selectedBrowser(); browser && browserRunning(browser->kind)) {
            latest = {};
            scanStatus->setText(QObject::tr("Close %1 before scanning its cache.").arg(browserName(browser->kind)));
            cleanButton->setEnabled(false);
            return;
        }
        latest = scanRegularFiles(cleanCategory->currentData().toString(), 5000);
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
    QObject::connect(cleanCategory, &QComboBox::currentIndexChanged, &window, [&] {
        table->setRowCount(0);
        latest = {};
        scanStatus->clear();
        cleanButton->setEnabled(false);
    });
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
        if (const auto *browser = selectedBrowser(); browser && browserRunning(browser->kind)) {
            scanStatus->setText(QObject::tr("Close %1 before removing its cache.").arg(browserName(browser->kind)));
            cleanButton->setEnabled(false);
            return;
        }
        const auto message = QObject::tr("Permanently remove %1 selected cache files (%2)?")
                                 .arg(selected.size()).arg(formatBytes(bytes));
        if (QMessageBox::question(&window, QObject::tr("Confirm cleanup"), message,
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        int removed = 0;
        QString firstError;
        for (int row : selected) {
            if (const auto *browser = selectedBrowser(); browser && browserRunning(browser->kind)) {
                firstError = QObject::tr("%1 started during cleanup; remaining files were left untouched.")
                                 .arg(browserName(browser->kind));
                break;
            }
            QString error;
            if (removeScannedFile(latest, latest.items[row], error)) ++removed;
            else if (firstError.isEmpty()) firstError = error;
        }
        scan();
        QMessageBox::information(&window, QObject::tr("Cleanup result"),
            QObject::tr("Removed %1 of %2 files. %3")
                .arg(removed).arg(selected.size()).arg(firstError));
    });
    QVector<TrashScan> latestTrashes;
    QVector<QPair<int, int>> trashRows;
    auto scanTrash = [&] {
        trashTable->setRowCount(0);
        trashRows.clear();
        QStringList mountRoots;
        for (const auto &volume : QStorageInfo::mountedVolumes())
            if (volume.isValid() && volume.isReady()) mountRoots.append(volume.rootPath());
        latestTrashes = scanTrashLocations(
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation),
            mountRoots, ::geteuid());
        if (!latestTrashes.isEmpty() && !latestTrashes.first().error.isEmpty()) {
            trashStatus->setText(QObject::tr("Trash unavailable: %1").arg(latestTrashes.first().error));
            trashRemove->setEnabled(false);
            return;
        }
        quint64 bytes = 0;
        bool hasRemovableItems = false;
        for (int scanIndex = 0; scanIndex < latestTrashes.size(); ++scanIndex) {
            const auto &scan = latestTrashes[scanIndex];
            for (int entryIndex = 0; entryIndex < scan.entries.size(); ++entryIndex) {
                const auto &item = scan.entries[entryIndex];
                const int row = trashTable->rowCount();
                trashTable->insertRow(row);
                trashRows.append({scanIndex, entryIndex});
                auto *name = new QTableWidgetItem(item.id);
                if (item.complete) {
                    name->setFlags(name->flags() | Qt::ItemIsUserCheckable);
                    name->setCheckState(Qt::Unchecked);
                    hasRemovableItems = true;
                } else {
                    name->setToolTip(QObject::tr("Item scan incomplete; this item cannot be removed safely."));
                }
                trashTable->setItem(row, 0, name);
                trashTable->setItem(row, 1, new QTableWidgetItem(scan.root));
                trashTable->setItem(row, 2, new QTableWidgetItem(item.originalPath));
                trashTable->setItem(row, 3, new QTableWidgetItem(formatBytes(item.size)));
                bytes += item.size;
            }
        }
        trashStatus->setText(QObject::tr("%1 items · about %2").arg(trashRows.size()).arg(formatBytes(bytes)));
        trashRemove->setEnabled(hasRemovableItems);
    };
    QObject::connect(trashScanButton, &QPushButton::clicked, &window, scanTrash);
    QObject::connect(trashRemove, &QPushButton::clicked, &window, [&] {
        QVector<int> selected;
        for (int row = 0; row < trashTable->rowCount(); ++row)
            if (trashTable->item(row, 0)->checkState() == Qt::Checked) selected.append(row);
        if (selected.isEmpty()) return;
        const QString prompt = QObject::tr("Permanently remove %1 selected trash items?").arg(selected.size());
        if (QMessageBox::question(&window, QObject::tr("Confirm permanent removal"), prompt,
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        int removed = 0;
        QString firstError;
        for (int row : selected) {
            QString error;
            const auto [scanIndex, entryIndex] = trashRows[row];
            const auto &scan = latestTrashes[scanIndex];
            if (removeTrashEntry(scan, scan.entries[entryIndex], error)) ++removed;
            else if (firstError.isEmpty()) firstError = error;
        }
        scanTrash();
        QMessageBox::information(&window, QObject::tr("Trash result"),
            QObject::tr("Removed %1 of %2 items. %3").arg(removed).arg(selected.size()).arg(firstError));
    });
    std::shared_ptr<std::atomic_bool> diskCancel;
    quint64 diskGeneration = 0;
    QObject::connect(chooseFolder, &QPushButton::clicked, &window, [&] {
        const QString chosen = QFileDialog::getExistingDirectory(&window, QObject::tr("Choose a folder to analyze"),
                                                                  diskPath->text());
        if (!chosen.isEmpty() && chosen != diskPath->text()) {
            if (diskCancel) diskCancel->store(true);
            ++diskGeneration;
            diskCancel.reset();
            diskPath->setText(chosen);
            diskTable->setRowCount(0);
            diskStatus->clear();
            analyze->setEnabled(true);
            cancelAnalysis->setEnabled(false);
        }
    });
    QObject::connect(cancelAnalysis, &QPushButton::clicked, &window, [&] {
        if (diskCancel) diskCancel->store(true);
        cancelAnalysis->setEnabled(false);
        diskStatus->setText(QObject::tr("Cancelling analysis…"));
    });
    QObject::connect(analyze, &QPushButton::clicked, &window, [&] {
        diskTable->setRowCount(0);
        const QString path = diskPath->text();
        auto token = std::make_shared<std::atomic_bool>(false);
        diskCancel = token;
        const quint64 generation = ++diskGeneration;
        analyze->setEnabled(false);
        cancelAnalysis->setEnabled(true);
        diskStatus->setText(QObject::tr("Scanning…"));
        auto *watcher = new QFutureWatcher<ScanResult>(&window);
        QObject::connect(watcher, &QFutureWatcher<ScanResult>::finished, &window,
                         [&, watcher, generation] {
            const ScanResult result = watcher->result();
            watcher->deleteLater();
            if (generation != diskGeneration) return;
            diskCancel.reset();
            analyze->setEnabled(true);
            cancelAnalysis->setEnabled(false);
            if (result.cancelled) {
                diskStatus->setText(QObject::tr("Analysis cancelled."));
                return;
            }
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
        watcher->setFuture(QtConcurrent::run([path, token] {
            return scanRegularFiles(path, 50000, token.get());
        }));
    });
    QVector<AutostartEntry> startupEntries;
    auto refreshStartup = [&] {
        const QString userConfig = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        const QString systemValue = qEnvironmentVariable("XDG_CONFIG_DIRS", "/etc/xdg");
        const QStringList systemDirs = systemValue.split(':', Qt::SkipEmptyParts);
        startupEntries = listAutostart(userConfig, systemDirs);
        startupTable->setRowCount(startupEntries.size());
        for (int row = 0; row < startupEntries.size(); ++row) {
            const auto &entry = startupEntries[row];
            startupTable->setItem(row, 0, new QTableWidgetItem(entry.name));
            startupTable->setItem(row, 1, new QTableWidgetItem(entry.enabled ? QObject::tr("Enabled") : QObject::tr("Disabled")));
            startupTable->setItem(row, 2, new QTableWidgetItem(entry.userEntry ? QObject::tr("User") : QObject::tr("System")));
        }
        startupToggle->setEnabled(false);
    };
    QObject::connect(startupRefresh, &QPushButton::clicked, &window, refreshStartup);
    QObject::connect(startupTable, &QTableWidget::itemSelectionChanged, &window, [&] {
        startupToggle->setEnabled(startupTable->currentRow() >= 0);
    });
    QObject::connect(startupToggle, &QPushButton::clicked, &window, [&] {
        const int row = startupTable->currentRow();
        if (row < 0 || row >= startupEntries.size()) return;
        const auto entry = startupEntries[row];
        const bool enable = !entry.enabled;
        const QString prompt = enable
            ? QObject::tr("Enable %1 at session startup?").arg(entry.name)
            : QObject::tr("Disable %1 at session startup?").arg(entry.name);
        if (QMessageBox::question(&window, QObject::tr("Confirm startup change"), prompt,
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        const QString userConfig = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        const QStringList systemDirs = qEnvironmentVariable("XDG_CONFIG_DIRS", "/etc/xdg")
                                           .split(':', Qt::SkipEmptyParts);
        QString error;
        if (!setAutostartEnabled(entry, enable, userConfig, systemDirs, error))
            QMessageBox::warning(&window, QObject::tr("Startup change failed"), error);
        refreshStartup();
    });
    refreshStartup();
    QVector<ProcessEntry> processEntries;
    auto refreshProcesses = [&] {
        processEntries = listProcesses();
        processTable->setRowCount(processEntries.size());
        for (int row = 0; row < processEntries.size(); ++row) {
            const auto &entry = processEntries[row];
            processTable->setItem(row, 0, new QTableWidgetItem(QString::number(entry.pid)));
            processTable->setItem(row, 1, new QTableWidgetItem(entry.name));
            processTable->setItem(row, 2, new QTableWidgetItem(QString::number(entry.uid)));
            processTable->setItem(row, 3, new QTableWidgetItem(formatBytes(entry.residentBytes)));
        }
        processTerminate->setEnabled(false);
    };
    QObject::connect(processRefresh, &QPushButton::clicked, &window, refreshProcesses);
    QObject::connect(processTable, &QTableWidget::itemSelectionChanged, &window, [&] {
        processTerminate->setEnabled(processTable->currentRow() >= 0);
    });
    QObject::connect(processTerminate, &QPushButton::clicked, &window, [&] {
        const int row = processTable->currentRow();
        if (row < 0 || row >= processEntries.size()) return;
        const auto entry = processEntries[row];
        const QString prompt = QObject::tr("Send a termination request to %1 (PID %2)?")
                                   .arg(entry.name).arg(entry.pid);
        if (QMessageBox::question(&window, QObject::tr("Confirm process action"), prompt,
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        QString error;
        if (!terminateOwnProcess(entry, error))
            QMessageBox::warning(&window, QObject::tr("Process action failed"), error);
        refreshProcesses();
    });
    refreshProcesses();
    QVector<ServiceEntry> serviceEntries;
    auto refreshServices = [&] {
        QString error;
        const bool user = serviceScope->currentIndex() == 0;
        serviceEntries = listServices(user, error);
        serviceTable->setRowCount(serviceEntries.size());
        for (int row = 0; row < serviceEntries.size(); ++row) {
            const auto &entry = serviceEntries[row];
            serviceTable->setItem(row, 0, new QTableWidgetItem(entry.unit));
            serviceTable->setItem(row, 1, new QTableWidgetItem(entry.active));
            serviceTable->setItem(row, 2, new QTableWidgetItem(entry.enabled));
            serviceTable->setItem(row, 3, new QTableWidgetItem(entry.description));
        }
        serviceStatus->setText(error.isEmpty() ? QObject::tr("%1 services").arg(serviceEntries.size()) : error);
        serviceToggle->setEnabled(false);
        serviceEnable->setEnabled(false);
    };
    QObject::connect(serviceRefresh, &QPushButton::clicked, &window, refreshServices);
    QObject::connect(serviceScope, &QComboBox::currentIndexChanged, &window, refreshServices);
    QObject::connect(serviceTable, &QTableWidget::itemSelectionChanged, &window, [&] {
        serviceToggle->setEnabled(serviceScope->currentIndex() == 0 && serviceTable->currentRow() >= 0);
        const int row = serviceTable->currentRow();
        serviceEnable->setEnabled(serviceScope->currentIndex() == 0 && row >= 0 &&
            row < serviceEntries.size() &&
            (serviceEntries[row].enabled == "enabled" || serviceEntries[row].enabled == "disabled"));
    });
    QObject::connect(serviceToggle, &QPushButton::clicked, &window, [&] {
        const int row = serviceTable->currentRow();
        if (row < 0 || row >= serviceEntries.size() || !serviceEntries[row].user) return;
        const auto entry = serviceEntries[row];
        const bool start = entry.active != "active";
        const QString prompt = start
            ? QObject::tr("Start user service %1?").arg(entry.unit)
            : QObject::tr("Stop user service %1?").arg(entry.unit);
        if (QMessageBox::question(&window, QObject::tr("Confirm service action"), prompt,
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        QString error;
        if (!setUserServiceRunning(entry.unit, start, error))
            QMessageBox::warning(&window, QObject::tr("Service action failed"), error);
        refreshServices();
    });
    QObject::connect(serviceEnable, &QPushButton::clicked, &window, [&] {
        const int row = serviceTable->currentRow();
        if (row < 0 || row >= serviceEntries.size() || !serviceEntries[row].user) return;
        const auto entry = serviceEntries[row];
        if (entry.enabled != "enabled" && entry.enabled != "disabled") return;
        const bool enable = entry.enabled == "disabled";
        const QString prompt = enable
            ? QObject::tr("Enable user service %1 at startup?").arg(entry.unit)
            : QObject::tr("Disable user service %1 at startup?").arg(entry.unit);
        if (QMessageBox::question(&window, QObject::tr("Confirm service startup change"), prompt,
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        QString error;
        if (!setUserServiceEnabled(entry.unit, enable, error))
            QMessageBox::warning(&window, QObject::tr("Service startup change failed"), error);
        refreshServices();
    });
    QObject::connect(tabs, &QTabWidget::currentChanged, &window, [&](int index) {
        if (tabs->widget(index) == servicePage) refreshServices();
    });
    QString reviewedPackage;
    QString reviewedVersion;
    QString reviewedFingerprint;
    auto refreshPackages = [&] {
        reviewedPackage.clear();
        reviewedVersion.clear();
        reviewedFingerprint.clear();
        removePackage->setEnabled(false);
        QString error;
        const auto packages = installedPackages(error);
        packageTable->setRowCount(packages.size());
        for (int row = 0; row < packages.size(); ++row) {
            packageTable->setItem(row, 0, new QTableWidgetItem(packages[row].name));
            packageTable->setItem(row, 1, new QTableWidgetItem(packages[row].version));
            packageTable->setRowHidden(row, !packages[row].name.contains(packageSearch->text(), Qt::CaseInsensitive));
        }
        packageStatus->setText(error.isEmpty() ? QObject::tr("%1 installed packages").arg(packages.size()) : error);
        simulateRemoval->setEnabled(false);
        removalPlan->clear();
    };
    QObject::connect(packageRefresh, &QPushButton::clicked, &window, refreshPackages);
    QObject::connect(packageSearch, &QLineEdit::textChanged, &window, [&](const QString &filter) {
        for (int row = 0; row < packageTable->rowCount(); ++row)
            packageTable->setRowHidden(row, !packageTable->item(row, 0)->text().contains(filter, Qt::CaseInsensitive));
    });
    QObject::connect(packageTable, &QTableWidget::itemSelectionChanged, &window, [&] {
        reviewedPackage.clear();
        reviewedVersion.clear();
        reviewedFingerprint.clear();
        removePackage->setEnabled(false);
        simulateRemoval->setEnabled(packageTable->currentRow() >= 0);
        removalPlan->clear();
    });
    QObject::connect(simulateRemoval, &QPushButton::clicked, &window, [&] {
        const int row = packageTable->currentRow();
        if (row < 0) return;
        const QString package = packageTable->item(row, 0)->text();
        QString error;
        const auto plan = simulatePackageRemoval(package, error);
        QString summary = error.isEmpty()
            ? QObject::tr("APT would remove:\n%1").arg(plan.removalDetails.join('\n')) : error;
        if (!plan.otherActions.isEmpty())
            summary += QObject::tr("\n\nAPT also plans these actions:\n%1").arg(plan.otherActions.join('\n'));
        removalPlan->setPlainText(summary);
        if (error.isEmpty()) {
            reviewedPackage = package;
            reviewedVersion = packageTable->item(row, 1)->text();
            reviewedFingerprint = removalPlanFingerprint(package, reviewedVersion, plan);
            removePackage->setEnabled(true);
        }
    });
    QObject::connect(removePackage, &QPushButton::clicked, &window, [&] {
        if (reviewedPackage.isEmpty() || reviewedFingerprint.isEmpty()) return;
        const QString prompt = QObject::tr("Remove %1 (version %2)?\n\n%3\n\n"
                                           "APT will recheck this plan after authorization. "
                                           "This action may remove dependent packages and cannot be undone automatically.")
                                   .arg(reviewedPackage, reviewedVersion, removalPlan->toPlainText());
        if (QMessageBox::question(&window, QObject::tr("Confirm package removal"), prompt,
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        auto *process = new QProcess(&window);
        removePackage->setEnabled(false);
        simulateRemoval->setEnabled(false);
        packageRefresh->setEnabled(false);
        packageStatus->setText(QObject::tr("Waiting for authorization and APT"));
        QObject::connect(process, &QProcess::finished, &window,
                         [&, process](int code, QProcess::ExitStatus status) {
            const QString output = QString::fromUtf8(process->readAllStandardOutput()).right(12000);
            const QString errors = QString::fromUtf8(process->readAllStandardError()).right(4000);
            if (status == QProcess::NormalExit && code == 0)
                QMessageBox::information(&window, QObject::tr("Package removal finished"), output);
            else
                QMessageBox::warning(&window, QObject::tr("Package removal failed"),
                                     errors.isEmpty() ? output : errors + "\n" + output);
            process->deleteLater();
            packageRefresh->setEnabled(true);
            refreshPackages();
        });
        QObject::connect(process, &QProcess::errorOccurred, &window,
                         [&, process](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart) return;
            QMessageBox::warning(&window, QObject::tr("Package removal failed"),
                                 QObject::tr("Could not start the authorization helper."));
            process->deleteLater();
            packageRefresh->setEnabled(true);
            refreshPackages();
        });
        process->start("/usr/bin/pkexec", {"/usr/libexec/syslinuxos-care-helper",
                                             "remove-package", reviewedPackage,
                                             reviewedVersion, reviewedFingerprint});
    });
    QObject::connect(tabs, &QTabWidget::currentChanged, &window, [&](int index) {
        if (tabs->widget(index) == packagePage && packageTable->rowCount() == 0) refreshPackages();
    });
    window.show();
    return app.exec();
}
