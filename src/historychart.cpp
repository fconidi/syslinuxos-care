#include "historychart.h"

#include <QPainter>
#include <QPainterPath>
#include <QPalette>

#include <algorithm>

HistoryChart::HistoryChart(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(150);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void HistoryChart::appendSample(double cpuPercent, double memoryPercent) {
    cpu_.append(std::clamp(cpuPercent, 0.0, 100.0));
    memory_.append(std::clamp(memoryPercent, 0.0, 100.0));
    if (cpu_.size() > 60) cpu_.removeFirst();
    if (memory_.size() > 60) memory_.removeFirst();
    update();
}

void HistoryChart::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF area = rect().adjusted(8, 8, -8, -8);
    painter.fillRect(area, palette().color(QPalette::Base));
    painter.setPen(QPen(palette().color(QPalette::Mid), 1));
    for (int line = 0; line <= 4; ++line) {
        const double y = area.top() + area.height() * line / 4.0;
        painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    }
    auto drawSeries = [&](const QVector<double> &samples, const QColor &color) {
        if (samples.size() < 2) return;
        QPainterPath path;
        for (int index = 0; index < samples.size(); ++index) {
            const double x = area.left() + area.width() * index / (samples.size() - 1);
            const double y = area.bottom() - area.height() * samples[index] / 100.0;
            if (index == 0) path.moveTo(x, y);
            else path.lineTo(x, y);
        }
        painter.setPen(QPen(color, 2));
        painter.drawPath(path);
    };
    drawSeries(cpu_, QColor(47, 118, 206));
    drawSeries(memory_, QColor(214, 109, 43));
}
