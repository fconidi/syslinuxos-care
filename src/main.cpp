#include "metrics.h"
#include "scanner.h"
#include "autostart.h"
#include "processes.h"
#include "services.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMainWindow>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("SysLinuxOS Care");
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
    auto *serviceTable = new QTableWidget(0, 3, servicePage);
    serviceTable->setHorizontalHeaderLabels({QObject::tr("Service"), QObject::tr("State"), QObject::tr("Description")});
    serviceTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    serviceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    serviceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    serviceTable->setSelectionMode(QAbstractItemView::SingleSelection);
    auto *serviceToggle = new QPushButton(QObject::tr("Start or stop selected"), servicePage);
    serviceToggle->setEnabled(false);
    serviceLayout->addWidget(serviceScope);
    serviceLayout->addWidget(serviceRefresh);
    serviceLayout->addWidget(serviceStatus);
    serviceLayout->addWidget(serviceTable);
    serviceLayout->addWidget(serviceToggle);
    tabs->addTab(servicePage, QObject::tr("Services"));
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
            serviceTable->setItem(row, 2, new QTableWidgetItem(entry.description));
        }
        serviceStatus->setText(error.isEmpty() ? QObject::tr("%1 services").arg(serviceEntries.size()) : error);
        serviceToggle->setEnabled(false);
    };
    QObject::connect(serviceRefresh, &QPushButton::clicked, &window, refreshServices);
    QObject::connect(serviceScope, &QComboBox::currentIndexChanged, &window, refreshServices);
    QObject::connect(serviceTable, &QTableWidget::itemSelectionChanged, &window, [&] {
        serviceToggle->setEnabled(serviceScope->currentIndex() == 0 && serviceTable->currentRow() >= 0);
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
    QObject::connect(tabs, &QTabWidget::currentChanged, &window, [&](int index) {
        if (tabs->widget(index) == servicePage) refreshServices();
    });
    window.show();
    return app.exec();
}
