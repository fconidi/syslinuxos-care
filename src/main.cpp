#include "metrics.h"

#include <QApplication>
#include <QFormLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QMainWindow window;
    window.setWindowTitle(QObject::tr("System maintenance"));
    auto *page = new QWidget(&window);
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
    window.setCentralWidget(page);
    window.resize(600, 360);

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
    window.show();
    return app.exec();
}
