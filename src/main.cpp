#include "metrics.h"
#include "scanner.h"
#include "autostart.h"
#include "processes.h"
#include "services.h"
#include "packages.h"
#include "aptcache.h"
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
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMainWindow>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QProgressBar>
#include <QProcess>
#include <QSettings>
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

class NumericItem : public QTableWidgetItem {
public:
    explicit NumericItem(const QString &text, quint64 value) : QTableWidgetItem(text) {
        setData(Qt::UserRole, value);
    }
    bool operator<(const QTableWidgetItem &other) const override {
        return data(Qt::UserRole).toULongLong() < other.data(Qt::UserRole).toULongLong();
    }
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("SysLinuxOS Care");
    QSettings settings("SysLinuxOS", "SysLinuxOS Care");
    const QString configuredLanguage = settings.value("language", "system").toString();
    const QString preferredLanguage = QStringList{"system", "en", "it", "fr", "de", "es"}
                                               .contains(configuredLanguage) ? configuredLanguage : "system";
    QTranslator translator;
    const QLocale displayLocale = preferredLanguage == "system" ? QLocale::system()
                                                                  : QLocale(preferredLanguage);
    if (preferredLanguage != "en" &&
        translator.load(displayLocale, "syslinuxos-care", "_", ":/i18n"))
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
    const int savedInterval = interval->findData(settings.value("monitorInterval", 2000).toInt());
    if (savedInterval >= 0) interval->setCurrentIndex(savedInterval);
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
    cleanCategory->addItem(QObject::tr("APT package archives"));
    auto *scanButton = new QPushButton(QObject::tr("Scan selected cache"), cleanPage);
    auto *cancelScan = new QPushButton(QObject::tr("Cancel scan"), cleanPage);
    cancelScan->setEnabled(false);
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
    cleanLayout->addWidget(cancelScan);
    cleanLayout->addWidget(scanStatus);
    cleanLayout->addWidget(table);
    cleanLayout->addWidget(cleanButton);
    tabs->addTab(cleanPage, QObject::tr("Cleanup"));

    auto *trashPage = new QWidget(tabs);
    auto *trashLayout = new QVBoxLayout(trashPage);
    trashLayout->addWidget(new QLabel(QObject::tr("Home and mounted-volume trash · permanent removal"), trashPage));
    trashLayout->addWidget(new QLabel(QObject::tr("Review the original location of each item. Nothing is selected automatically."), trashPage));
    auto *trashScanButton = new QPushButton(QObject::tr("Scan trash"), trashPage);
    auto *trashCancel = new QPushButton(QObject::tr("Cancel scan"), trashPage);
    trashCancel->setEnabled(false);
    auto *trashStatus = new QLabel(trashPage);
    auto *trashTable = new QTableWidget(0, 4, trashPage);
    trashTable->setHorizontalHeaderLabels({QObject::tr("Item"), QObject::tr("Trash location"),
                                          QObject::tr("Original location"), QObject::tr("Size")});
    trashTable->setColumnWidth(0, 140);
    trashTable->setColumnWidth(1, 200);
    trashTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    trashTable->setColumnWidth(3, 90);
    trashTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    auto *trashRemove = new QPushButton(QObject::tr("Permanently remove selected"), trashPage);
    trashRemove->setEnabled(false);
    trashLayout->addWidget(trashScanButton);
    trashLayout->addWidget(trashCancel);
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
    auto *processSearch = new QLineEdit(processPage);
    processSearch->setPlaceholderText(QObject::tr("Filter processes"));
    processSearch->setAccessibleName(QObject::tr("Filter processes"));
    auto *processRefresh = new QPushButton(QObject::tr("Refresh processes"), processPage);
    auto *processStatus = new QLabel(processPage);
    auto *processTable = new QTableWidget(0, 4, processPage);
    processTable->setHorizontalHeaderLabels({QObject::tr("PID"), QObject::tr("Name"), QObject::tr("UID"), QObject::tr("Memory")});
    processTable->setColumnWidth(0, 80);
    processTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    processTable->setColumnWidth(2, 90);
    processTable->setColumnWidth(3, 120);
    processTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    processTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    processTable->setSelectionMode(QAbstractItemView::SingleSelection);
    processTable->setSortingEnabled(true);
    auto *processTerminate = new QPushButton(QObject::tr("Request termination"), processPage);
    processTerminate->setEnabled(false);
    processLayout->addWidget(processSearch);
    processLayout->addWidget(processRefresh);
    processLayout->addWidget(processStatus);
    processLayout->addWidget(processTable);
    processLayout->addWidget(processTerminate);
    tabs->addTab(processPage, QObject::tr("Processes"));

    auto *servicePage = new QWidget(tabs);
    auto *serviceLayout = new QVBoxLayout(servicePage);
    auto *serviceScope = new QComboBox(servicePage);
    serviceScope->addItem(QObject::tr("User services"));
    serviceScope->addItem(QObject::tr("System services"));
    auto *serviceRefresh = new QPushButton(QObject::tr("Refresh services"), servicePage);
    auto *serviceStatus = new QLabel(servicePage);
    auto *serviceTable = new QTableWidget(0, 4, servicePage);
    serviceTable->setHorizontalHeaderLabels({QObject::tr("Service"), QObject::tr("State"), QObject::tr("Startup"), QObject::tr("Description")});
    serviceTable->setColumnWidth(0, 230);
    serviceTable->setColumnWidth(1, 90);
    serviceTable->setColumnWidth(2, 100);
    serviceTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    serviceTable->setWordWrap(false);
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

    auto *settingsPage = new QWidget(tabs);
    auto *settingsLayout = new QVBoxLayout(settingsPage);
    auto *languageLabel = new QLabel(QObject::tr("Interface language"), settingsPage);
    auto *languageChoice = new QComboBox(settingsPage);
    languageLabel->setBuddy(languageChoice);
    settingsLayout->addWidget(languageLabel);
    languageChoice->addItem(QObject::tr("System language (automatic)"), "system");
    languageChoice->addItem(QStringLiteral("English"), "en");
    languageChoice->addItem(QStringLiteral("Italiano"), "it");
    languageChoice->addItem(QStringLiteral("Français"), "fr");
    languageChoice->addItem(QStringLiteral("Deutsch"), "de");
    languageChoice->addItem(QStringLiteral("Español"), "es");
    const int savedLanguage = languageChoice->findData(preferredLanguage);
    if (savedLanguage >= 0) languageChoice->setCurrentIndex(savedLanguage);
    settingsLayout->addWidget(languageChoice);
    auto *languageStatus = new QLabel(settingsPage);
    settingsLayout->addWidget(languageStatus);
    auto *intervalLabel = new QLabel(QObject::tr("Default monitoring interval"), settingsPage);
    auto *settingsInterval = new QComboBox(settingsPage);
    intervalLabel->setBuddy(settingsInterval);
    settingsLayout->addWidget(intervalLabel);
    for (int index = 0; index < interval->count(); ++index)
        settingsInterval->addItem(interval->itemText(index), interval->itemData(index));
    settingsInterval->setCurrentIndex(interval->currentIndex());
    settingsLayout->addWidget(settingsInterval);
    settingsLayout->addStretch();
    tabs->addTab(settingsPage, QObject::tr("Settings"));
    for (auto *dataTable : {table, trashTable, diskTable, startupTable,
                            processTable, serviceTable, packageTable})
        dataTable->verticalHeader()->hide();
    table->setAccessibleName(QObject::tr("Cleanup"));
    trashTable->setAccessibleName(QObject::tr("Trash"));
    diskTable->setAccessibleName(QObject::tr("Disk space"));
    startupTable->setAccessibleName(QObject::tr("Startup"));
    processTable->setAccessibleName(QObject::tr("Processes"));
    serviceTable->setAccessibleName(QObject::tr("Services"));
    packageTable->setAccessibleName(QObject::tr("Packages"));
    packageSearch->setAccessibleName(QObject::tr("Filter installed packages"));
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
        settingsInterval->setCurrentIndex(interval->currentIndex());
        settings.setValue("monitorInterval", interval->currentData().toInt());
        settings.sync();
    });
    QObject::connect(settingsInterval, &QComboBox::currentIndexChanged, &window, [&] {
        interval->setCurrentIndex(settingsInterval->currentIndex());
    });
    QObject::connect(languageChoice, &QComboBox::currentIndexChanged, &window, [&] {
        settings.setValue("language", languageChoice->currentData().toString());
        settings.sync();
        languageStatus->setText(settings.status() == QSettings::NoError
            ? QObject::tr("Restart SysLinuxOS Care to apply the language change.")
            : QObject::tr("Could not save the language setting."));
    });
    timer.start(interval->currentData().toInt());
    update();

    ScanResult latest;
    const auto cacheHome = qEnvironmentVariable("XDG_CACHE_HOME");
    const QString cachePath = QDir::isAbsolutePath(cacheHome) ? cacheHome : QDir::homePath() + "/.cache";
    cleanCategory->setItemData(0, QDir(cachePath).filePath("thumbnails"));
    cleanCategory->setItemData(1, QDir(cachePath).filePath("fontconfig"));
    cleanCategory->setItemData(2, aptArchiveDirectory());
    const auto browserCaches = discoverBrowserCaches(cachePath);
    for (const auto &browser : browserCaches)
        cleanCategory->addItem(QObject::tr("Browser cache: %1 (%2)")
                                   .arg(browserName(browser.kind), browser.profile), browser.path);
    const auto browserData = discoverBrowserData(
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation), QDir::homePath());
    const int browserDataStart = 3 + browserCaches.size();
    for (const auto &data : browserData) {
        const QString label = data.kind == BrowserDataKind::Cookies
            ? QObject::tr("Browser cookies: %1 (%2)")
            : QObject::tr("Browser history: %1 (%2)");
        cleanCategory->addItem(label.arg(browserName(data.browser), data.profile), data.root);
    }
    auto selectedBrowser = [&]() -> const BrowserCache * {
        const int index = cleanCategory->currentIndex() - 3;
        return index >= 0 && index < browserCaches.size() ? &browserCaches[index] : nullptr;
    };
    auto selectedBrowserData = [&]() -> const BrowserData * {
        const int index = cleanCategory->currentIndex() - browserDataStart;
        return index >= 0 && index < browserData.size() ? &browserData[index] : nullptr;
    };
    std::shared_ptr<std::atomic_bool> cacheCancel;
    quint64 cacheGeneration = 0;
    auto scan = [&] {
        if (cacheCancel) cacheCancel->store(true);
        const quint64 generation = ++cacheGeneration;
        table->setRowCount(0);
        latest = {};
        cleanButton->setEnabled(false);
        if (const auto *browser = selectedBrowser(); browser && browserRunning(browser->kind)) {
            scanStatus->setText(QObject::tr("Close %1 before scanning its cache.").arg(browserName(browser->kind)));
            cacheCancel.reset();
            scanButton->setEnabled(true);
            cancelScan->setEnabled(false);
            return;
        }
        if (const auto *data = selectedBrowserData(); data && browserRunning(data->browser)) {
            scanStatus->setText(QObject::tr("Close %1 before scanning its profile.")
                                    .arg(browserName(data->browser)));
            cacheCancel.reset();
            scanButton->setEnabled(true);
            cancelScan->setEnabled(false);
            return;
        }
        const bool hasBrowserData = selectedBrowserData() != nullptr;
        const BrowserData data = hasBrowserData ? *selectedBrowserData() : BrowserData{};
        const int category = cleanCategory->currentIndex();
        const QString path = cleanCategory->currentData().toString();
        auto token = std::make_shared<std::atomic_bool>(false);
        cacheCancel = token;
        scanButton->setEnabled(false);
        cancelScan->setEnabled(true);
        scanStatus->setText(QObject::tr("Scanning…"));
        auto *watcher = new QFutureWatcher<ScanResult>(&window);
        QObject::connect(watcher, &QFutureWatcher<ScanResult>::finished, &window,
                         [&, watcher, generation, token] {
            const ScanResult result = watcher->result();
            watcher->deleteLater();
            if (generation != cacheGeneration) return;
            cacheCancel.reset();
            scanButton->setEnabled(true);
            cancelScan->setEnabled(false);
            if (token->load() || result.cancelled) {
                scanStatus->setText(QObject::tr("Scan cancelled."));
                return;
            }
            latest = result;
            if (!latest.error.isEmpty()) {
                scanStatus->setText(QObject::tr("Scan unavailable: %1").arg(latest.error));
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
        });
        watcher->setFuture(QtConcurrent::run([hasBrowserData, data, category, path, token] {
            if (hasBrowserData) return scanBrowserData(data, token.get());
            if (category == 2) return scanAptArchives(path, token.get());
            return scanRegularFiles(path, 5000, token.get());
        }));
    };
    QObject::connect(scanButton, &QPushButton::clicked, &window, scan);
    QObject::connect(cancelScan, &QPushButton::clicked, &window, [&] {
        if (cacheCancel) cacheCancel->store(true);
        cancelScan->setEnabled(false);
        scanStatus->setText(QObject::tr("Cancelling scan…"));
    });
    QObject::connect(cleanCategory, &QComboBox::currentIndexChanged, &window, [&] {
        if (cacheCancel) cacheCancel->store(true);
        ++cacheGeneration;
        cacheCancel.reset();
        table->setRowCount(0);
        latest = {};
        scanStatus->clear();
        cleanButton->setEnabled(false);
        scanButton->setEnabled(true);
        cancelScan->setEnabled(false);
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
        if (const auto *data = selectedBrowserData(); data && browserRunning(data->browser)) {
            scanStatus->setText(QObject::tr("Close %1 before changing its profile.")
                                    .arg(browserName(data->browser)));
            cleanButton->setEnabled(false);
            return;
        }
        const auto *data = selectedBrowserData();
        if (data && selected.size() != latest.items.size()) {
            QMessageBox::warning(&window, QObject::tr("Incomplete browser selection"),
                QObject::tr("Select every file shown for this browser database, including its sidecar files."));
            return;
        }
        const bool aptArchives = cleanCategory->currentIndex() == 2;
        const auto message = data && data->kind == BrowserDataKind::Cookies
            ? QObject::tr("Permanently delete cookies for %1 profile %2? "
                          "This may sign you out. No backup is created. Other site data remains.")
                  .arg(browserName(data->browser), data->profile)
            : data && data->kind == BrowserDataKind::History
            ? QObject::tr("Permanently delete the History database for %1 profile %2? "
                          "This also removes its download list. Other browsing traces may remain. "
                          "No backup is created.")
                  .arg(browserName(data->browser), data->profile)
            : aptArchives
            ? QObject::tr("Permanently remove %1 selected APT archives (%2)? "
                          "These packages may need to be downloaded again.")
                  .arg(selected.size()).arg(formatBytes(bytes))
            : QObject::tr("Permanently remove %1 selected cache files (%2)?")
                  .arg(selected.size()).arg(formatBytes(bytes));
        if (QMessageBox::question(&window, QObject::tr("Confirm cleanup"), message,
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        if (data) {
            const ScanResult current = scanBrowserData(*data);
            if (!current.error.isEmpty() || browserDataFingerprint(current).isEmpty() ||
                browserDataFingerprint(current) != browserDataFingerprint(latest)) {
                QMessageBox::warning(&window, QObject::tr("Browser profile changed"),
                                     QObject::tr("The browser database changed after the preview. Scan it again."));
                cleanButton->setEnabled(false);
                return;
            }
        }
        if (aptArchives) {
            QJsonArray names;
            for (int row : selected) names.append(latest.items[row].relativePath);
            const QString fingerprint = aptArchiveFingerprint(latest);
            if (fingerprint.isEmpty()) return;
            auto *process = new QProcess(&window);
            cleanCategory->setEnabled(false);
            scanButton->setEnabled(false);
            cleanButton->setEnabled(false);
            table->setEnabled(false);
            scanStatus->setText(QObject::tr("Waiting for authorization and APT cache cleanup"));
            QObject::connect(process, &QProcess::finished, &window,
                             [&, process](int code, QProcess::ExitStatus status) {
                const QString output = QString::fromUtf8(process->readAllStandardOutput()).right(12000);
                const QString errors = QString::fromUtf8(process->readAllStandardError()).right(4000);
                if (status == QProcess::NormalExit && code == 0)
                    QMessageBox::information(&window, QObject::tr("APT cache cleanup finished"), output);
                else
                    QMessageBox::warning(&window, QObject::tr("APT cache cleanup failed"),
                                         errors.isEmpty() ? output : errors + "\n" + output);
                process->deleteLater();
                cleanCategory->setEnabled(true);
                scanButton->setEnabled(true);
                table->setEnabled(true);
                scan();
            });
            QObject::connect(process, &QProcess::errorOccurred, &window,
                             [&, process](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart) return;
                QMessageBox::warning(&window, QObject::tr("APT cache cleanup failed"),
                                     QObject::tr("Could not start the authorization helper."));
                process->deleteLater();
                cleanCategory->setEnabled(true);
                scanButton->setEnabled(true);
                table->setEnabled(true);
                scan();
            });
            process->start("/usr/bin/pkexec", {"/usr/libexec/syslinuxos-care-helper",
                                                 "clean-apt-cache", fingerprint});
            process->write(QJsonDocument(names).toJson(QJsonDocument::Compact));
            process->closeWriteChannel();
            return;
        }
        int removed = 0;
        QString firstError;
        for (int row : selected) {
            if (const auto *browser = selectedBrowser(); browser && browserRunning(browser->kind)) {
                firstError = QObject::tr("%1 started during cleanup; remaining files were left untouched.")
                                 .arg(browserName(browser->kind));
                break;
            }
            if (data && browserRunning(data->browser)) {
                firstError = QObject::tr("%1 started during cleanup; remaining profile files were left untouched.")
                                 .arg(browserName(data->browser));
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
    std::shared_ptr<std::atomic_bool> trashCancelToken;
    quint64 trashGeneration = 0;
    auto scanTrash = [&] {
        if (trashCancelToken) trashCancelToken->store(true);
        const quint64 generation = ++trashGeneration;
        trashTable->setRowCount(0);
        trashRows.clear();
        latestTrashes.clear();
        trashRemove->setEnabled(false);
        trashScanButton->setEnabled(false);
        trashCancel->setEnabled(true);
        trashStatus->setText(QObject::tr("Scanning…"));
        const QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        const quint32 userId = ::geteuid();
        auto token = std::make_shared<std::atomic_bool>(false);
        trashCancelToken = token;
        auto *watcher = new QFutureWatcher<QVector<TrashScan>>(&window);
        QObject::connect(watcher, &QFutureWatcher<QVector<TrashScan>>::finished, &window,
                         [&, watcher, generation, token] {
            const QVector<TrashScan> result = watcher->result();
            watcher->deleteLater();
            if (generation != trashGeneration) return;
            trashCancelToken.reset();
            trashScanButton->setEnabled(true);
            trashCancel->setEnabled(false);
            if (token->load()) {
                trashStatus->setText(QObject::tr("Scan cancelled."));
                return;
            }
            latestTrashes = result;
            if (!latestTrashes.isEmpty() && !latestTrashes.first().error.isEmpty()) {
                trashStatus->setText(QObject::tr("Trash unavailable: %1").arg(latestTrashes.first().error));
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
                    auto *location = new QTableWidgetItem(scan.root);
                    location->setToolTip(scan.root);
                    trashTable->setItem(row, 1, location);
                    auto *original = new QTableWidgetItem(item.originalPath);
                    original->setToolTip(item.originalPath);
                    trashTable->setItem(row, 2, original);
                    trashTable->setItem(row, 3, new QTableWidgetItem(formatBytes(item.size)));
                    bytes += item.size;
                }
            }
            trashStatus->setText(QObject::tr("%1 items · about %2").arg(trashRows.size()).arg(formatBytes(bytes)));
            trashRemove->setEnabled(hasRemovableItems);
        });
        watcher->setFuture(QtConcurrent::run([dataHome, userId, token] {
            QStringList mountRoots;
            for (const auto &volume : QStorageInfo::mountedVolumes()) {
                if (token->load()) return QVector<TrashScan>{};
                if (volume.isValid() && volume.isReady()) mountRoots.append(volume.rootPath());
            }
            return scanTrashLocations(dataHome, mountRoots, userId, token.get());
        }));
    };
    QObject::connect(trashScanButton, &QPushButton::clicked, &window, scanTrash);
    QObject::connect(trashCancel, &QPushButton::clicked, &window, [&] {
        if (trashCancelToken) trashCancelToken->store(true);
        trashCancel->setEnabled(false);
        trashStatus->setText(QObject::tr("Cancelling scan…"));
    });
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
    quint64 processGeneration = 0;
    auto filterProcesses = [&](const QString &filter) {
        processTable->clearSelection();
        processTerminate->setEnabled(false);
        for (int row = 0; row < processTable->rowCount(); ++row) {
            const bool match = processTable->item(row, 0)->text().contains(filter, Qt::CaseInsensitive) ||
                               processTable->item(row, 1)->text().contains(filter, Qt::CaseInsensitive);
            processTable->setRowHidden(row, !match);
        }
    };
    auto refreshProcesses = [&] {
        const quint64 generation = ++processGeneration;
        processEntries.clear();
        processTable->setSortingEnabled(false);
        processTable->setRowCount(0);
        processTerminate->setEnabled(false);
        processStatus->setText(QObject::tr("Loading processes…"));
        auto *watcher = new QFutureWatcher<QVector<ProcessEntry>>(&window);
        QObject::connect(watcher, &QFutureWatcher<QVector<ProcessEntry>>::finished, &window,
                         [&, watcher, generation] {
            const auto entries = watcher->result();
            watcher->deleteLater();
            if (generation != processGeneration) return;
            processEntries = entries;
            processTable->setRowCount(processEntries.size());
            for (int row = 0; row < processEntries.size(); ++row) {
                const auto &entry = processEntries[row];
                auto *pid = new NumericItem(QString::number(entry.pid), entry.pid);
                pid->setData(Qt::UserRole + 1, row);
                processTable->setItem(row, 0, pid);
                auto *name = new QTableWidgetItem(entry.name);
                name->setToolTip(entry.name);
                processTable->setItem(row, 1, name);
                processTable->setItem(row, 2, new NumericItem(QString::number(entry.uid), entry.uid));
                processTable->setItem(row, 3, new NumericItem(formatBytes(entry.residentBytes),
                                                               entry.residentBytes));
            }
            processTable->setSortingEnabled(true);
            filterProcesses(processSearch->text());
            processStatus->setText(QObject::tr("%1 processes").arg(processEntries.size()));
        });
        watcher->setFuture(QtConcurrent::run([] { return listProcesses(); }));
    };
    QObject::connect(processRefresh, &QPushButton::clicked, &window, refreshProcesses);
    QObject::connect(processSearch, &QLineEdit::textChanged, &window, filterProcesses);
    QObject::connect(processTable, &QTableWidget::itemSelectionChanged, &window, [&] {
        const int row = processTable->currentRow();
        processTerminate->setEnabled(row >= 0 && !processTable->isRowHidden(row));
    });
    QObject::connect(processTerminate, &QPushButton::clicked, &window, [&] {
        const int row = processTable->currentRow();
        if (row < 0 || processTable->isRowHidden(row)) return;
        const int index = processTable->item(row, 0)->data(Qt::UserRole + 1).toInt();
        if (index < 0 || index >= processEntries.size()) return;
        const auto entry = processEntries[index];
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
    quint64 serviceGeneration = 0;
    auto refreshServices = [&] {
        const quint64 generation = ++serviceGeneration;
        const bool user = serviceScope->currentIndex() == 0;
        serviceEntries.clear();
        serviceTable->setRowCount(0);
        serviceToggle->setEnabled(false);
        serviceEnable->setEnabled(false);
        serviceStatus->setText(QObject::tr("Loading services…"));
        using ServiceResult = QPair<QVector<ServiceEntry>, QString>;
        auto *watcher = new QFutureWatcher<ServiceResult>(&window);
        QObject::connect(watcher, &QFutureWatcher<ServiceResult>::finished, &window,
                         [&, watcher, generation] {
            const auto [entries, error] = watcher->result();
            watcher->deleteLater();
            if (generation != serviceGeneration) return;
            serviceEntries = entries;
            serviceTable->setRowCount(serviceEntries.size());
            for (int row = 0; row < serviceEntries.size(); ++row) {
                const auto &entry = serviceEntries[row];
                auto *unit = new QTableWidgetItem(entry.unit);
                unit->setToolTip(entry.unit);
                serviceTable->setItem(row, 0, unit);
                serviceTable->setItem(row, 1, new QTableWidgetItem(entry.active));
                serviceTable->setItem(row, 2, new QTableWidgetItem(entry.enabled));
                auto *description = new QTableWidgetItem(entry.description);
                description->setToolTip(entry.description);
                serviceTable->setItem(row, 3, description);
            }
            serviceStatus->setText(error.isEmpty() ? QObject::tr("%1 services").arg(serviceEntries.size()) : error);
        });
        watcher->setFuture(QtConcurrent::run([user] {
            QString error;
            auto entries = listServices(user, error);
            return ServiceResult{entries, error};
        }));
    };
    QObject::connect(serviceRefresh, &QPushButton::clicked, &window, refreshServices);
    QObject::connect(serviceScope, &QComboBox::currentIndexChanged, &window, refreshServices);
    auto runUserServiceAction = [&](const ServiceEntry &entry, bool desired, bool startup) {
        serviceScope->setEnabled(false);
        serviceRefresh->setEnabled(false);
        serviceTable->setEnabled(false);
        serviceToggle->setEnabled(false);
        serviceEnable->setEnabled(false);
        serviceStatus->setText(QObject::tr("Applying user service action…"));
        using ActionResult = QPair<bool, QString>;
        auto *watcher = new QFutureWatcher<ActionResult>(&window);
        QObject::connect(watcher, &QFutureWatcher<ActionResult>::finished, &window,
                         [&, watcher, startup] {
            const auto [succeeded, error] = watcher->result();
            watcher->deleteLater();
            if (!succeeded)
                QMessageBox::warning(&window, startup
                    ? QObject::tr("Service startup change failed")
                    : QObject::tr("Service action failed"), error);
            serviceScope->setEnabled(true);
            serviceRefresh->setEnabled(true);
            serviceTable->setEnabled(true);
            refreshServices();
        });
        watcher->setFuture(QtConcurrent::run([entry, desired, startup] {
            QString error;
            const auto current = listServices(true, error);
            if (!error.isEmpty()) return ActionResult{false, error};
            const auto found = std::find_if(current.cbegin(), current.cend(), [&](const ServiceEntry &item) {
                return item.unit == entry.unit;
            });
            if (!entry.user || !validServiceUnit(entry.unit) || found == current.cend() ||
                (startup ? found->enabled != entry.enabled : found->active != entry.active))
                return ActionResult{false, QObject::tr("Service state changed; refresh the list.")};
            const bool succeeded = startup
                ? setUserServiceEnabled(entry.unit, desired, error)
                : setUserServiceRunning(entry.unit, desired, error);
            return ActionResult{succeeded, error};
        }));
    };
    QObject::connect(serviceTable, &QTableWidget::itemSelectionChanged, &window, [&] {
        serviceToggle->setEnabled(serviceScope->currentIndex() == 0 && serviceTable->currentRow() >= 0);
        const int row = serviceTable->currentRow();
        serviceEnable->setEnabled(row >= 0 &&
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
        runUserServiceAction(entry, start, false);
    });
    QObject::connect(serviceEnable, &QPushButton::clicked, &window, [&] {
        const int row = serviceTable->currentRow();
        if (row < 0 || row >= serviceEntries.size()) return;
        const auto entry = serviceEntries[row];
        if (entry.enabled != "enabled" && entry.enabled != "disabled") return;
        const bool enable = entry.enabled == "disabled";
        QString prompt;
        if (entry.user)
            prompt = enable
                ? QObject::tr("Enable user service %1 at startup?").arg(entry.unit)
                : QObject::tr("Disable user service %1 at startup?").arg(entry.unit);
        else
            prompt = QObject::tr("Change system service %1 from %2 to %3 at startup?\n\n"
                                 "This will not start or stop the running service. "
                                 "Related startup links may also change.")
                         .arg(entry.unit, entry.enabled,
                              enable ? QObject::tr("enabled") : QObject::tr("disabled"));
        if (QMessageBox::question(&window, QObject::tr("Confirm service startup change"), prompt,
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        if (!entry.user) {
            auto *process = new QProcess(&window);
            serviceScope->setEnabled(false);
            serviceRefresh->setEnabled(false);
            serviceTable->setEnabled(false);
            serviceToggle->setEnabled(false);
            serviceEnable->setEnabled(false);
            serviceStatus->setText(QObject::tr("Waiting for authorization and service manager"));
            QObject::connect(process, &QProcess::finished, &window,
                             [&, process](int code, QProcess::ExitStatus status) {
                const QString output = QString::fromUtf8(process->readAllStandardOutput()).right(4000);
                const QString errors = QString::fromUtf8(process->readAllStandardError()).right(4000);
                if (status == QProcess::NormalExit && code == 0)
                    QMessageBox::information(&window, QObject::tr("Service startup change finished"),
                                             QObject::tr("The service startup setting was updated.") + "\n" + output);
                else
                    QMessageBox::warning(&window, QObject::tr("Service startup change failed"),
                                         errors.isEmpty() ? output : errors + "\n" + output);
                process->deleteLater();
                serviceScope->setEnabled(true);
                serviceRefresh->setEnabled(true);
                serviceTable->setEnabled(true);
                refreshServices();
            });
            QObject::connect(process, &QProcess::errorOccurred, &window,
                             [&, process](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart) return;
                QMessageBox::warning(&window, QObject::tr("Service startup change failed"),
                                     QObject::tr("Could not start the authorization helper."));
                process->deleteLater();
                serviceScope->setEnabled(true);
                serviceRefresh->setEnabled(true);
                serviceTable->setEnabled(true);
                refreshServices();
            });
            process->start("/usr/bin/pkexec", {"/usr/libexec/syslinuxos-care-helper",
                                                 "set-service-enabled", entry.unit, entry.enabled,
                                                 enable ? "enabled" : "disabled"});
            return;
        }
        runUserServiceAction(entry, enable, true);
    });
    QObject::connect(tabs, &QTabWidget::currentChanged, &window, [&](int index) {
        if (tabs->widget(index) == servicePage) refreshServices();
    });
    QString reviewedPackage;
    QString reviewedVersion;
    QString reviewedFingerprint;
    quint64 packageGeneration = 0;
    quint64 simulationGeneration = 0;
    auto refreshPackages = [&] {
        const quint64 generation = ++packageGeneration;
        ++simulationGeneration;
        reviewedPackage.clear();
        reviewedVersion.clear();
        reviewedFingerprint.clear();
        removePackage->setEnabled(false);
        packageTable->setRowCount(0);
        simulateRemoval->setEnabled(false);
        removalPlan->clear();
        packageStatus->setText(QObject::tr("Loading packages…"));
        using PackageResult = QPair<QVector<PackageEntry>, QString>;
        auto *watcher = new QFutureWatcher<PackageResult>(&window);
        QObject::connect(watcher, &QFutureWatcher<PackageResult>::finished, &window,
                         [&, watcher, generation] {
            const auto [packages, error] = watcher->result();
            watcher->deleteLater();
            if (generation != packageGeneration) return;
            packageTable->setRowCount(packages.size());
            for (int row = 0; row < packages.size(); ++row) {
                packageTable->setItem(row, 0, new QTableWidgetItem(packages[row].name));
                packageTable->setItem(row, 1, new QTableWidgetItem(packages[row].version));
                packageTable->setRowHidden(row, !packages[row].name.contains(packageSearch->text(), Qt::CaseInsensitive));
            }
            packageStatus->setText(error.isEmpty() ? QObject::tr("%1 installed packages").arg(packages.size()) : error);
        });
        watcher->setFuture(QtConcurrent::run([] {
            QString error;
            auto packages = installedPackages(error);
            return PackageResult{packages, error};
        }));
    };
    QObject::connect(packageRefresh, &QPushButton::clicked, &window, refreshPackages);
    QObject::connect(packageSearch, &QLineEdit::textChanged, &window, [&](const QString &filter) {
        for (int row = 0; row < packageTable->rowCount(); ++row)
            packageTable->setRowHidden(row, !packageTable->item(row, 0)->text().contains(filter, Qt::CaseInsensitive));
    });
    QObject::connect(packageTable, &QTableWidget::itemSelectionChanged, &window, [&] {
        ++simulationGeneration;
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
        const QString version = packageTable->item(row, 1)->text();
        const quint64 generation = ++simulationGeneration;
        reviewedPackage.clear();
        reviewedVersion.clear();
        reviewedFingerprint.clear();
        removePackage->setEnabled(false);
        simulateRemoval->setEnabled(false);
        removalPlan->setPlainText(QObject::tr("Simulating APT removal…"));
        using SimulationResult = QPair<PackageRemovalPlan, QString>;
        auto *watcher = new QFutureWatcher<SimulationResult>(&window);
        QObject::connect(watcher, &QFutureWatcher<SimulationResult>::finished, &window,
                         [&, watcher, generation, package, version] {
            const auto [plan, error] = watcher->result();
            watcher->deleteLater();
            if (generation != simulationGeneration) return;
            const int selectedRow = packageTable->currentRow();
            if (selectedRow < 0 || packageTable->item(selectedRow, 0)->text() != package ||
                packageTable->item(selectedRow, 1)->text() != version) return;
            simulateRemoval->setEnabled(true);
            QString summary = error.isEmpty()
                ? QObject::tr("APT would remove:\n%1").arg(plan.removalDetails.join('\n')) : error;
            if (!plan.otherActions.isEmpty())
                summary += QObject::tr("\n\nAPT also plans these actions:\n%1").arg(plan.otherActions.join('\n'));
            removalPlan->setPlainText(summary);
            if (error.isEmpty()) {
                reviewedPackage = package;
                reviewedVersion = version;
                reviewedFingerprint = removalPlanFingerprint(package, version, plan);
                removePackage->setEnabled(!reviewedFingerprint.isEmpty());
            }
        });
        watcher->setFuture(QtConcurrent::run([package] {
            QString error;
            auto plan = simulatePackageRemoval(package, error);
            return SimulationResult{plan, error};
        }));
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
