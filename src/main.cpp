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
#include "metricgauge.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMainWindow>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QProcess>
#include <QSettings>
#include <QSignalBlocker>
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

void setAllCheckableRows(QTableWidget *table, Qt::CheckState state) {
    const QSignalBlocker blocked(table);
    for (int row = 0; row < table->rowCount(); ++row) {
        auto *item = table->item(row, 0);
        if (item && (item->flags() & Qt::ItemIsUserCheckable)) item->setCheckState(state);
    }
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("SysLinuxOS Care");
    app.setWindowIcon(QIcon(":/icons/syslinuxos-care.png"));
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
    window.setWindowIcon(app.windowIcon());
    auto *tabs = new QTabWidget(&window);
    auto *page = new QWidget(tabs);
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(20, 16, 20, 16);
    outer->setSpacing(12);
    auto *title = new QLabel(QObject::tr("System overview"), page);
    QFont titleFont = title->font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    title->setFont(titleFont);
    auto *header = new QHBoxLayout;
    auto *brandMark = new QLabel(page);
    brandMark->setPixmap(app.windowIcon().pixmap(40, 40));
    brandMark->setFixedSize(44, 44);
    brandMark->setAlignment(Qt::AlignCenter);
    brandMark->setAccessibleName(QStringLiteral("SysLinuxOS Care"));
    header->addWidget(brandMark);
    header->addSpacing(4);
    header->addWidget(title);
    header->addStretch();
    auto *refresh = new QPushButton(QObject::tr("Refresh now"), page);
    header->addWidget(refresh);
    outer->addLayout(header);
    auto *network = new QLabel(page);
    auto *meterPanel = new QFrame(page);
    meterPanel->setFrameShape(QFrame::StyledPanel);
    auto *meterLayout = new QHBoxLayout(meterPanel);
    meterLayout->setContentsMargins(16, 8, 16, 8);
    auto *cpuGauge = new MetricGauge(QObject::tr("CPU"), QColor(47, 118, 206), meterPanel);
    auto *memoryGauge = new MetricGauge(QObject::tr("Memory"), QColor(214, 109, 43), meterPanel);
    auto *diskGauge = new MetricGauge(QObject::tr("Root disk"), QColor(49, 150, 126), meterPanel);
    meterLayout->addWidget(cpuGauge);
    meterLayout->addWidget(memoryGauge);
    meterLayout->addWidget(diskGauge);
    outer->addWidget(meterPanel);
    auto *networkRow = new QHBoxLayout;
    auto *networkTitle = new QLabel(QObject::tr("Network"), page);
    QFont sectionFont = networkTitle->font();
    sectionFont.setBold(true);
    networkTitle->setFont(sectionFont);
    networkRow->addWidget(networkTitle);
    networkRow->addSpacing(12);
    networkRow->addWidget(network, 1);
    outer->addLayout(networkRow);
    auto *historyTitle = new QLabel(QObject::tr("Usage history"), page);
    historyTitle->setFont(sectionFont);
    outer->addWidget(historyTitle);
    auto *history = new HistoryChart(QObject::tr("CPU"), QObject::tr("Memory"), page);
    history->setAccessibleName(QObject::tr("CPU and memory usage history"));
    outer->addWidget(history, 1);
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
    auto *overviewIntervalLabel = new QLabel(QObject::tr("Update interval"), page);
    overviewIntervalLabel->setBuddy(interval);
    controls->addWidget(overviewIntervalLabel);
    controls->addWidget(interval);
    controls->addStretch();
    outer->addLayout(controls);
    tabs->addTab(page, QObject::tr("Overview"));

    auto *cleanPage = new QWidget(tabs);
    auto *cleanLayout = new QVBoxLayout(cleanPage);
    auto *categoryLabel = new QLabel(QObject::tr("Select a cleanup category"), cleanPage);
    auto *cleanCategory = new QComboBox(cleanPage);
    categoryLabel->setBuddy(cleanCategory);
    cleanLayout->addWidget(categoryLabel);
    cleanLayout->addWidget(new QLabel(QObject::tr("Review each file before removing it. All items start unselected."), cleanPage));
    cleanCategory->addItem(QObject::tr("Thumbnail cache"));
    cleanCategory->addItem(QObject::tr("Font cache"));
    cleanCategory->addItem(QObject::tr("APT package archives"));
    auto *scanButton = new QPushButton(QObject::tr("Scan selected category"), cleanPage);
    auto *cancelScan = new QPushButton(QObject::tr("Cancel scan"), cleanPage);
    cancelScan->setEnabled(false);
    auto *cleanButton = new QPushButton(QObject::tr("Remove selected"), cleanPage);
    cleanButton->setEnabled(false);
    auto *cancelCleanup = new QPushButton(QObject::tr("Cancel cleanup"), cleanPage);
    cancelCleanup->setEnabled(false);
    auto *cleanSelectAll = new QPushButton(QObject::tr("Select all"), cleanPage);
    auto *cleanClearSelection = new QPushButton(QObject::tr("Clear selection"), cleanPage);
    cleanSelectAll->setEnabled(false);
    cleanClearSelection->setEnabled(false);
    auto *cleanSelectionStatus = new QLabel(cleanPage);
    auto *scanStatus = new QLabel(cleanPage);
    auto *table = new QTableWidget(0, 2, cleanPage);
    table->setHorizontalHeaderLabels({QObject::tr("File"), QObject::tr("Size")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    cleanLayout->addWidget(cleanCategory);
    auto *cleanScanActions = new QHBoxLayout;
    cleanScanActions->addWidget(scanButton);
    cleanScanActions->addWidget(cancelScan);
    cleanScanActions->addStretch();
    cleanLayout->addLayout(cleanScanActions);
    cleanLayout->addWidget(scanStatus);
    auto *cleanSelectionActions = new QHBoxLayout;
    cleanSelectionActions->addWidget(cleanSelectAll);
    cleanSelectionActions->addWidget(cleanClearSelection);
    cleanSelectionActions->addStretch();
    cleanSelectionActions->addWidget(cleanSelectionStatus);
    cleanLayout->addLayout(cleanSelectionActions);
    cleanLayout->addWidget(table);
    auto *cleanRemovalActions = new QHBoxLayout;
    cleanRemovalActions->addStretch();
    cleanRemovalActions->addWidget(cancelCleanup);
    cleanRemovalActions->addWidget(cleanButton);
    cleanLayout->addLayout(cleanRemovalActions);
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
    auto *trashSelectAll = new QPushButton(QObject::tr("Select all"), trashPage);
    auto *trashClearSelection = new QPushButton(QObject::tr("Clear selection"), trashPage);
    trashSelectAll->setEnabled(false);
    trashClearSelection->setEnabled(false);
    auto *trashSelectionStatus = new QLabel(trashPage);
    auto *trashScanActions = new QHBoxLayout;
    trashScanActions->addWidget(trashScanButton);
    trashScanActions->addWidget(trashCancel);
    trashScanActions->addStretch();
    trashLayout->addLayout(trashScanActions);
    trashLayout->addWidget(trashStatus);
    auto *trashSelectionActions = new QHBoxLayout;
    trashSelectionActions->addWidget(trashSelectAll);
    trashSelectionActions->addWidget(trashClearSelection);
    trashSelectionActions->addStretch();
    trashSelectionActions->addWidget(trashSelectionStatus);
    trashLayout->addLayout(trashSelectionActions);
    trashLayout->addWidget(trashTable);
    auto *trashRemovalActions = new QHBoxLayout;
    trashRemovalActions->addStretch();
    trashRemovalActions->addWidget(trashRemove);
    trashLayout->addLayout(trashRemovalActions);
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
    diskTable->setColumnWidth(1, 140);
    diskTable->setSortingEnabled(true);
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
    auto *startupStatus = new QLabel(startupPage);
    auto *startupTable = new QTableWidget(0, 3, startupPage);
    startupTable->setHorizontalHeaderLabels({QObject::tr("Application"), QObject::tr("Status"), QObject::tr("Source")});
    startupTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    startupTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    startupTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    startupTable->setSelectionMode(QAbstractItemView::SingleSelection);
    startupLayout->addWidget(startupRefresh);
    startupLayout->addWidget(startupStatus);
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
    serviceScope->setAccessibleName(QObject::tr("Services"));
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
    window.resize(960, 620);

    MetricsReader reader;
    auto update = [&] {
        const Metrics m = reader.sample();
        cpuGauge->setReading(m.cpuAvailable && m.cpuPercent >= 0 ? m.cpuPercent : -1,
                             !m.cpuAvailable ? QObject::tr("Unavailable")
                             : m.cpuPercent < 0 ? QObject::tr("Collecting…") : QString{});
        memoryGauge->setReading(m.memoryTotal ? 100.0 * m.memoryUsed / m.memoryTotal : -1,
                                 m.memoryTotal ? formatBytes(m.memoryUsed) + " / " + formatBytes(m.memoryTotal)
                                               : QObject::tr("Unavailable"));
        diskGauge->setReading(m.diskTotal ? 100.0 * m.diskUsed / m.diskTotal : -1,
                               m.diskTotal ? formatBytes(m.diskUsed) + " / " + formatBytes(m.diskTotal)
                                           : QObject::tr("Unavailable"));
        network->setText(!m.networkAvailable ? QObject::tr("Unavailable")
                           : !m.networkRateAvailable ? QObject::tr("Collecting…")
                           : QObject::tr("Down: %1/s · Up: %2/s")
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
    QVector<ScanResult> allScans;
    QVector<QPair<int, int>> allRows;
    int allCategoryIndex = -1;
    auto updateCleanSelection = [&] {
        int selected = 0;
        quint64 bytes = 0;
        const int total = table->rowCount();
        for (int row = 0; row < total; ++row) {
            const auto *item = table->item(row, 0);
            if (item && item->checkState() == Qt::Checked &&
                (cleanCategory->currentIndex() == allCategoryIndex ? row < allRows.size()
                                                               : row < latest.items.size())) {
                ++selected;
                bytes += cleanCategory->currentIndex() == allCategoryIndex
                    ? allScans[allRows[row].first].items[allRows[row].second].size
                    : latest.items[row].size;
            }
        }
        cleanSelectionStatus->setText(QObject::tr("%1 of %2 selected · %3")
                                          .arg(selected).arg(total).arg(formatBytes(bytes)));
        const bool ready = table->isEnabled() && latest.error.isEmpty();
        cleanSelectAll->setEnabled(ready && total > selected);
        cleanClearSelection->setEnabled(ready && selected > 0);
        cleanButton->setEnabled(ready && selected > 0);
    };
    QObject::connect(table, &QTableWidget::itemChanged, &window,
                     [&](QTableWidgetItem *item) { if (item->column() == 0) updateCleanSelection(); });
    QObject::connect(cleanSelectAll, &QPushButton::clicked, &window, [&] {
        setAllCheckableRows(table, Qt::Checked);
        updateCleanSelection();
    });
    QObject::connect(cleanClearSelection, &QPushButton::clicked, &window, [&] {
        setAllCheckableRows(table, Qt::Unchecked);
        updateCleanSelection();
    });
    updateCleanSelection();
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
    allCategoryIndex = cleanCategory->count();
    cleanCategory->addItem(QObject::tr("ALL — all cleanup categories"));
    auto selectedBrowser = [&]() -> const BrowserCache * {
        const int index = cleanCategory->currentIndex() - 3;
        return index >= 0 && index < browserCaches.size() ? &browserCaches[index] : nullptr;
    };
    auto selectedBrowserData = [&]() -> const BrowserData * {
        const int index = cleanCategory->currentIndex() - browserDataStart;
        return index >= 0 && index < browserData.size() ? &browserData[index] : nullptr;
    };
    std::shared_ptr<std::atomic_bool> cacheCancel;
    std::shared_ptr<std::atomic_bool> cleanupCancel;
    QObject::connect(cancelCleanup, &QPushButton::clicked, &window, [&] {
        if (!cleanupCancel) return;
        cleanupCancel->store(true);
        cancelCleanup->setEnabled(false);
        scanStatus->setText(QObject::tr("Stopping cleanup after the current file…"));
    });
    quint64 cacheGeneration = 0;
    auto scan = [&] {
        if (cacheCancel) cacheCancel->store(true);
        const quint64 generation = ++cacheGeneration;
        table->setRowCount(0);
        latest = {};
        allScans.clear();
        allRows.clear();
        updateCleanSelection();
        if (cleanCategory->currentIndex() == allCategoryIndex) {
            auto token = std::make_shared<std::atomic_bool>(false);
            cacheCancel = token;
            scanButton->setEnabled(false);
            cancelScan->setEnabled(true);
            scanStatus->setText(QObject::tr("Scanning all cleanup categories…"));
            QStringList paths;
            QStringList labels;
            for (int index = 0; index < allCategoryIndex; ++index) {
                paths.append(cleanCategory->itemData(index).toString());
                labels.append(cleanCategory->itemText(index));
            }
            auto *watcher = new QFutureWatcher<QVector<ScanResult>>(&window);
            QObject::connect(watcher, &QFutureWatcher<QVector<ScanResult>>::finished, &window,
                             [&, watcher, generation, token, labels] {
                const auto results = watcher->result();
                watcher->deleteLater();
                if (generation != cacheGeneration) return;
                cacheCancel.reset();
                scanButton->setEnabled(true);
                cancelScan->setEnabled(false);
                if (token->load()) {
                    scanStatus->setText(QObject::tr("Scan cancelled."));
                    return;
                }
                for (int index = 0; index < results.size(); ++index) {
                    if (!results[index].error.isEmpty()) {
                        scanStatus->setText(QObject::tr("Scan unavailable in %1: %2")
                                                .arg(labels[index], results[index].error));
                        return;
                    }
                }
                allScans = results;
                quint64 bytes = 0;
                bool truncated = false;
                const QSignalBlocker blocked(table);
                for (int category = 0; category < allScans.size(); ++category) {
                    const auto &result = allScans[category];
                    truncated |= result.truncated;
                    for (int itemIndex = 0; itemIndex < result.items.size(); ++itemIndex) {
                        const int row = table->rowCount();
                        const auto &item = result.items[itemIndex];
                        allRows.append({category, itemIndex});
                        table->insertRow(row);
                        auto *name = new QTableWidgetItem(labels[category] + " / " + item.relativePath);
                        name->setFlags(name->flags() | Qt::ItemIsUserCheckable);
                        name->setCheckState(Qt::Unchecked);
                        table->setItem(row, 0, name);
                        table->setItem(row, 1, new QTableWidgetItem(formatBytes(item.size)));
                        bytes += item.size;
                    }
                }
                scanStatus->setText(QObject::tr("%1 files · %2%3")
                                        .arg(allRows.size()).arg(formatBytes(bytes),
                                            truncated ? QObject::tr(" · scan limit reached") : QString{}));
                updateCleanSelection();
            });
            watcher->setFuture(QtConcurrent::run([paths, browserCaches, browserData,
                                                   browserDataStart, token] {
                QVector<ScanResult> results;
                for (int index = 0; index < paths.size(); ++index) {
                    if (token->load()) break;
                    if (index >= browserDataStart) {
                        const auto &data = browserData[index - browserDataStart];
                        if (browserRunning(data.browser)) {
                            ScanResult unavailable;
                            unavailable.error = QObject::tr("Close %1 before scanning its profile.")
                                                    .arg(browserName(data.browser));
                            results.append(unavailable);
                            break;
                        }
                        results.append(scanBrowserData(data, token.get()));
                    } else if (index >= 3) {
                        const auto &browser = browserCaches[index - 3];
                        if (browserRunning(browser.kind)) {
                            ScanResult unavailable;
                            unavailable.error = QObject::tr("Close %1 before scanning its cache.")
                                                    .arg(browserName(browser.kind));
                            results.append(unavailable);
                            break;
                        }
                        results.append(scanRegularFiles(paths[index], 5000, token.get()));
                    } else if (!QFileInfo::exists(paths[index])) {
                        results.append(ScanResult{});
                    } else if (index == 2) {
                        results.append(scanAptArchives(paths[index], token.get()));
                    } else {
                        results.append(scanRegularFiles(paths[index], 5000, token.get()));
                    }
                    if (!results.last().error.isEmpty() || results.last().cancelled) break;
                }
                return results;
            }));
            return;
        }
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
            const QSignalBlocker blocked(table);
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
            updateCleanSelection();
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
        allScans.clear();
        allRows.clear();
        scanStatus->clear();
        updateCleanSelection();
        scanButton->setEnabled(true);
        cancelScan->setEnabled(false);
    });
    QObject::connect(cleanButton, &QPushButton::clicked, &window, [&] {
        if (cleanCategory->currentIndex() == allCategoryIndex) {
            QVector<QVector<ScanItem>> chosen(allScans.size());
            quint64 bytes = 0;
            int count = 0;
            for (int row = 0; row < allRows.size(); ++row) {
                if (table->item(row, 0)->checkState() != Qt::Checked) continue;
                const auto [category, itemIndex] = allRows[row];
                const auto &item = allScans[category].items[itemIndex];
                chosen[category].append(item);
                bytes += item.size;
                ++count;
            }
            if (!count) return;
            for (int category = browserDataStart; category < chosen.size(); ++category) {
                if (!chosen[category].isEmpty() &&
                    chosen[category].size() != allScans[category].items.size()) {
                    QMessageBox::warning(&window, QObject::tr("Incomplete browser selection"),
                        QObject::tr("Select every file shown for a browser database, including its sidecar files."));
                    return;
                }
            }
            for (int category = 3; category < chosen.size(); ++category) {
                if (chosen[category].isEmpty()) continue;
                const BrowserKind kind = category >= browserDataStart
                    ? browserData[category - browserDataStart].browser
                    : browserCaches[category - 3].kind;
                if (browserRunning(kind)) {
                    scanStatus->setText(QObject::tr("Close %1 before cleaning its files.")
                                            .arg(browserName(kind)));
                    cleanButton->setEnabled(false);
                    return;
                }
            }
            if (QMessageBox::question(&window, QObject::tr("Confirm cleanup"),
                    QObject::tr("Permanently remove %1 selected files from all cleanup categories (%2)? "
                                "Cookies may sign you out, browser history and download lists may be lost, "
                                "and APT packages may need to be downloaded again. No backup is created.")
                        .arg(count).arg(formatBytes(bytes)),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
            for (int category = browserDataStart; category < chosen.size(); ++category) {
                if (chosen[category].isEmpty()) continue;
                const auto current = scanBrowserData(browserData[category - browserDataStart]);
                if (!current.error.isEmpty() || browserDataFingerprint(current).isEmpty() ||
                    browserDataFingerprint(current) != browserDataFingerprint(allScans[category])) {
                    QMessageBox::warning(&window, QObject::tr("Browser profile changed"),
                        QObject::tr("A browser database changed after the preview. Scan again."));
                    cleanButton->setEnabled(false);
                    return;
                }
            }
            const auto previews = allScans;
            const int aptCount = chosen[2].size();
            const int fileCount = count - aptCount;
            auto startFiles = [&, previews, chosen, fileCount, aptCount] {
                cleanCategory->setEnabled(false);
                scanButton->setEnabled(false);
                cleanButton->setEnabled(false);
                table->setEnabled(false);
                updateCleanSelection();
                scanStatus->setText(QObject::tr("Removing selected files…"));
                auto token = std::make_shared<std::atomic_bool>(false);
                cleanupCancel = token;
                cancelCleanup->setEnabled(true);
                struct Result { int removed; QString firstError; bool cancelled; };
                auto *watcher = new QFutureWatcher<Result>(&window);
                QObject::connect(watcher, &QFutureWatcher<Result>::finished, &window,
                                 [&, watcher, fileCount, aptCount] {
                    const auto result = watcher->result();
                    watcher->deleteLater();
                    cleanupCancel.reset();
                    cancelCleanup->setEnabled(false);
                    cleanCategory->setEnabled(true);
                    scanButton->setEnabled(true);
                    table->setEnabled(true);
                    scan();
                    const QString message = result.cancelled
                        ? QObject::tr("Stopped after removing %1 of %2 selected files.")
                              .arg(result.removed).arg(fileCount)
                        : QObject::tr("Removed %1 of %2 files. %3")
                              .arg(result.removed).arg(fileCount).arg(result.firstError);
                    const QString summary = aptCount
                        ? QObject::tr("%1 APT archives removed. ").arg(aptCount) + message
                        : message;
                    QMessageBox::information(&window,
                        result.cancelled ? QObject::tr("Cleanup cancelled") : QObject::tr("Cleanup result"),
                        result.firstError.isEmpty() ? summary : summary + "\n" + result.firstError);
                });
                watcher->setFuture(QtConcurrent::run([previews, chosen, browserCaches, browserData,
                                                       browserDataStart, token] {
                    int removed = 0;
                    QString firstError;
                    for (int category = 0; category < chosen.size(); ++category) {
                        if (category == 2) continue; // APT archives are handled by the privileged helper.
                        QElapsedTimer browserCheckTimer;
                        browserCheckTimer.start();
                        const bool browser = category >= 3;
                        const bool profileData = category >= browserDataStart;
                        const BrowserKind kind = profileData
                            ? browserData[category - browserDataStart].browser
                            : browser ? browserCaches[category - 3].kind : BrowserKind::Firefox;
                        for (int index = 0; index < chosen[category].size(); ++index) {
                            if (token->load()) return Result{removed, firstError, true};
                            const bool checkNow = browser &&
                                (profileData || index % 64 == 0 || browserCheckTimer.elapsed() >= 50);
                            if (checkNow && browserRunning(kind)) {
                                if (firstError.isEmpty())
                                    firstError = QObject::tr("%1 started during cleanup; its remaining files were left untouched.")
                                                     .arg(browserName(kind));
                                break;
                            }
                            if (checkNow) browserCheckTimer.restart();
                            QString error;
                            if (removeScannedFile(previews[category], chosen[category][index], error)) ++removed;
                            else if (firstError.isEmpty()) firstError = error;
                        }
                    }
                    return Result{removed, firstError, false};
                }));
            };
            if (chosen.size() <= 2 || chosen[2].isEmpty()) {
                startFiles();
                return;
            }
            const QString fingerprint = aptArchiveFingerprint(previews[2]);
            if (fingerprint.isEmpty()) {
                QMessageBox::warning(&window, QObject::tr("APT cache cleanup failed"),
                                     QObject::tr("APT archive preview is no longer valid. Scan again."));
                return;
            }
            QJsonArray names;
            for (const auto &item : chosen[2]) names.append(item.relativePath);
            auto *process = new QProcess(&window);
            cleanCategory->setEnabled(false);
            scanButton->setEnabled(false);
            cleanButton->setEnabled(false);
            table->setEnabled(false);
            updateCleanSelection();
            scanStatus->setText(QObject::tr("Waiting for authorization and APT cache cleanup"));
            auto handled = std::make_shared<bool>(false);
            QObject::connect(process, &QProcess::finished, &window,
                             [&, process, handled, startFiles, fileCount](int code, QProcess::ExitStatus status) {
                if (*handled) return;
                *handled = true;
                const QString output = QString::fromUtf8(process->readAllStandardOutput()).right(12000);
                const QString errors = QString::fromUtf8(process->readAllStandardError()).right(4000);
                process->deleteLater();
                if (status == QProcess::NormalExit && code == 0) {
                    if (fileCount) startFiles();
                    else {
                        QMessageBox::information(&window, QObject::tr("APT cache cleanup finished"), output);
                        cleanCategory->setEnabled(true);
                        scanButton->setEnabled(true);
                        table->setEnabled(true);
                        scan();
                    }
                } else {
                    QMessageBox::warning(&window, QObject::tr("APT cache cleanup failed"),
                                         errors.isEmpty() ? output : errors + "\n" + output);
                    cleanCategory->setEnabled(true);
                    scanButton->setEnabled(true);
                    table->setEnabled(true);
                    scan();
                }
            });
            QObject::connect(process, &QProcess::errorOccurred, &window,
                             [&, process, handled](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart || *handled) return;
                *handled = true;
                process->deleteLater();
                QMessageBox::warning(&window, QObject::tr("APT cache cleanup failed"),
                                     QObject::tr("Could not start the authorization helper."));
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
            updateCleanSelection();
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
        const ScanResult preview = latest;
        QVector<ScanItem> items;
        items.reserve(selected.size());
        for (int row : selected) items.append(latest.items[row]);
        const auto *browser = selectedBrowser();
        const bool checkBrowser = browser || data;
        const bool profileData = data != nullptr;
        const BrowserKind browserKind = data ? data->browser
            : browser ? browser->kind : BrowserKind::Firefox;
        cleanCategory->setEnabled(false);
        scanButton->setEnabled(false);
        cleanButton->setEnabled(false);
        table->setEnabled(false);
        updateCleanSelection();
        scanStatus->setText(QObject::tr("Removing selected files…"));
        auto token = std::make_shared<std::atomic_bool>(false);
        cleanupCancel = token;
        cancelCleanup->setEnabled(true);
        struct CleanupResult { int removed; QString firstError; bool cancelled; };
        auto *watcher = new QFutureWatcher<CleanupResult>(&window);
        QObject::connect(watcher, &QFutureWatcher<CleanupResult>::finished, &window,
                         [&, watcher, count = selected.size()] {
            const auto [removed, firstError, cancelled] = watcher->result();
            watcher->deleteLater();
            cleanupCancel.reset();
            cancelCleanup->setEnabled(false);
            cleanCategory->setEnabled(true);
            scanButton->setEnabled(true);
            table->setEnabled(true);
            scan();
            const QString message = cancelled
                ? QObject::tr("Stopped after removing %1 of %2 selected files.").arg(removed).arg(count)
                : QObject::tr("Removed %1 of %2 files. %3").arg(removed).arg(count).arg(firstError);
            QMessageBox::information(&window,
                                     cancelled ? QObject::tr("Cleanup cancelled") : QObject::tr("Cleanup result"),
                                     firstError.isEmpty() || !cancelled ? message : message + "\n" + firstError);
        });
        watcher->setFuture(QtConcurrent::run([preview, items, checkBrowser, profileData, browserKind, token] {
            int removed = 0;
            QString firstError;
            QElapsedTimer browserCheckTimer;
            browserCheckTimer.start();
            for (int index = 0; index < items.size(); ++index) {
                const auto &item = items[index];
                if (token->load()) return CleanupResult{removed, firstError, true};
                // Database files are few and checked individually. Cache files are checked
                // in small batches so a large cleanup does not enumerate /proc per file.
                const bool checkNow = checkBrowser &&
                    (profileData || index % 64 == 0 || browserCheckTimer.elapsed() >= 50);
                if (checkNow && browserRunning(browserKind)) {
                    firstError = profileData
                        ? QObject::tr("%1 started during cleanup; remaining profile files were left untouched.")
                              .arg(browserName(browserKind))
                        : QObject::tr("%1 started during cleanup; remaining files were left untouched.")
                              .arg(browserName(browserKind));
                    break;
                }
                if (checkNow) browserCheckTimer.restart();
                QString error;
                if (removeScannedFile(preview, item, error)) ++removed;
                else if (firstError.isEmpty()) firstError = error;
            }
            return CleanupResult{removed, firstError, false};
        }));
    });
    QVector<TrashScan> latestTrashes;
    QVector<QPair<int, int>> trashRows;
    auto updateTrashSelection = [&] {
        int total = 0;
        int selected = 0;
        quint64 bytes = 0;
        for (int row = 0; row < trashRows.size(); ++row) {
            const auto [scanIndex, entryIndex] = trashRows[row];
            const auto &entry = latestTrashes[scanIndex].entries[entryIndex];
            if (!entry.complete) continue;
            ++total;
            const auto *item = trashTable->item(row, 0);
            if (item && item->checkState() == Qt::Checked) {
                ++selected;
                bytes += entry.size;
            }
        }
        trashSelectionStatus->setText(QObject::tr("%1 of %2 selected · %3")
                                          .arg(selected).arg(total).arg(formatBytes(bytes)));
        const bool ready = trashTable->isEnabled();
        trashSelectAll->setEnabled(ready && total > selected);
        trashClearSelection->setEnabled(ready && selected > 0);
        trashRemove->setEnabled(ready && selected > 0);
    };
    QObject::connect(trashTable, &QTableWidget::itemChanged, &window,
                     [&](QTableWidgetItem *item) { if (item->column() == 0) updateTrashSelection(); });
    QObject::connect(trashSelectAll, &QPushButton::clicked, &window, [&] {
        setAllCheckableRows(trashTable, Qt::Checked);
        updateTrashSelection();
    });
    QObject::connect(trashClearSelection, &QPushButton::clicked, &window, [&] {
        setAllCheckableRows(trashTable, Qt::Unchecked);
        updateTrashSelection();
    });
    updateTrashSelection();
    std::shared_ptr<std::atomic_bool> trashCancelToken;
    quint64 trashGeneration = 0;
    auto scanTrash = [&] {
        if (trashCancelToken) trashCancelToken->store(true);
        const quint64 generation = ++trashGeneration;
        trashTable->setRowCount(0);
        trashRows.clear();
        latestTrashes.clear();
        updateTrashSelection();
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
            const QSignalBlocker blocked(trashTable);
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
                    } else {
                        name->setFlags(name->flags() & ~Qt::ItemIsUserCheckable);
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
            updateTrashSelection();
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
        QVector<QPair<TrashScan, TrashEntry>> entries;
        entries.reserve(selected.size());
        for (int row : selected) {
            const auto [scanIndex, entryIndex] = trashRows[row];
            const auto &scan = latestTrashes[scanIndex];
            entries.append({scan, scan.entries[entryIndex]});
        }
        trashScanButton->setEnabled(false);
        trashTable->setEnabled(false);
        updateTrashSelection();
        trashStatus->setText(QObject::tr("Removing selected trash items…"));
        using TrashRemovalResult = QPair<int, QString>;
        auto *watcher = new QFutureWatcher<TrashRemovalResult>(&window);
        QObject::connect(watcher, &QFutureWatcher<TrashRemovalResult>::finished, &window,
                         [&, watcher, count = selected.size()] {
            const auto [removed, firstError] = watcher->result();
            watcher->deleteLater();
            trashScanButton->setEnabled(true);
            trashTable->setEnabled(true);
            scanTrash();
            QMessageBox::information(&window, QObject::tr("Trash result"),
                QObject::tr("Removed %1 of %2 items. %3").arg(removed).arg(count).arg(firstError));
        });
        watcher->setFuture(QtConcurrent::run([entries] {
            int removed = 0;
            QString firstError;
            for (const auto &[scan, entry] : entries) {
                QString error;
                if (removeTrashEntry(scan, entry, error)) ++removed;
                else if (firstError.isEmpty()) firstError = error;
            }
            return TrashRemovalResult{removed, firstError};
        }));
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
            diskTable->setSortingEnabled(false);
            diskTable->setRowCount(groups.size());
            for (int row = 0; row < groups.size(); ++row) {
                auto *name = new QTableWidgetItem(groups[row].first);
                name->setToolTip(groups[row].first);
                diskTable->setItem(row, 0, name);
                diskTable->setItem(row, 1, new NumericItem(formatBytes(groups[row].second), groups[row].second));
            }
            diskTable->setSortingEnabled(true);
            diskTable->sortByColumn(1, Qt::DescendingOrder);
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
        startupEntries.clear();
        startupTable->setRowCount(0);
        startupRefresh->setEnabled(false);
        startupTable->setEnabled(false);
        startupToggle->setEnabled(false);
        startupStatus->setText(QObject::tr("Loading startup applications…"));
        auto *watcher = new QFutureWatcher<QVector<AutostartEntry>>(&window);
        QObject::connect(watcher, &QFutureWatcher<QVector<AutostartEntry>>::finished, &window,
                         [&, watcher] {
            startupEntries = watcher->result();
            watcher->deleteLater();
            startupTable->setRowCount(startupEntries.size());
            for (int row = 0; row < startupEntries.size(); ++row) {
                const auto &entry = startupEntries[row];
                startupTable->setItem(row, 0, new QTableWidgetItem(entry.name));
                startupTable->setItem(row, 1, new QTableWidgetItem(entry.enabled ? QObject::tr("Enabled") : QObject::tr("Disabled")));
                startupTable->setItem(row, 2, new QTableWidgetItem(entry.userEntry ? QObject::tr("User") : QObject::tr("System")));
            }
            startupStatus->setText(QObject::tr("%1 startup applications").arg(startupEntries.size()));
            startupRefresh->setEnabled(true);
            startupTable->setEnabled(true);
        });
        watcher->setFuture(QtConcurrent::run([userConfig, systemDirs] {
            return listAutostart(userConfig, systemDirs);
        }));
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
        startupRefresh->setEnabled(false);
        startupToggle->setEnabled(false);
        startupTable->setEnabled(false);
        startupStatus->setText(QObject::tr("Updating startup application…"));
        using StartupResult = QPair<bool, QString>;
        auto *watcher = new QFutureWatcher<StartupResult>(&window);
        QObject::connect(watcher, &QFutureWatcher<StartupResult>::finished, &window,
                         [&, watcher] {
            const auto [succeeded, error] = watcher->result();
            watcher->deleteLater();
            refreshStartup();
            if (!succeeded)
                QMessageBox::warning(&window, QObject::tr("Startup change failed"), error);
        });
        watcher->setFuture(QtConcurrent::run([entry, enable, userConfig, systemDirs] {
            QString error;
            const bool succeeded = setAutostartEnabled(entry, enable, userConfig, systemDirs, error);
            return StartupResult{succeeded, error};
        }));
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
