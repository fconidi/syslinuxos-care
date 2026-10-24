#include "historychart.h"

#include <QPainter>
#include <QPainterPath>
#include <QPalette>

#include <algorithm>

HistoryChart::HistoryChart(const QString &cpuLabel, const QString &memoryLabel, QWidget *parent)
    : QWidget(parent), cpuLabel_(cpuLabel), memoryLabel_(memoryLabel) {
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
    const int axisWidth = painter.fontMetrics().horizontalAdvance(QStringLiteral("100%")) + 12;
    const QRectF area(axisWidth, 28, width() - axisWidth - 8, height() - 52);
    painter.fillRect(rect(), palette().color(QPalette::Base));
    painter.setPen(QPen(palette().color(QPalette::Mid), 1));
    for (int line = 0; line <= 4; ++line) {
        const double y = area.top() + area.height() * line / 4.0;
        painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        if (line % 2 == 0) {
            painter.setPen(palette().color(QPalette::Text));
            painter.drawText(QRectF(0, y - 10, axisWidth - 5, 20),
                             Qt::AlignRight | Qt::AlignVCenter,
                             QString::number(100 - line * 25) + "%");
            painter.setPen(QPen(palette().color(QPalette::Mid), 1));
        }
    }
    const QColor cpuColor(47, 118, 206);
    const QColor memoryColor(214, 109, 43);
    auto drawLegend = [&](const QString &label, const QColor &color, Qt::PenStyle style, int x) {
        painter.setPen(QPen(color, 2, style));
        painter.drawLine(QPointF(x, 15), QPointF(x + 18, 15));
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(x + 24, 20, label);
    };
    drawLegend(cpuLabel_, cpuColor, Qt::SolidLine, int(area.left()));
    drawLegend(memoryLabel_, memoryColor, Qt::DashLine,
               int(area.left()) + painter.fontMetrics().horizontalAdvance(cpuLabel_) + 45);
    auto drawSeries = [&](const QVector<double> &samples, const QColor &color, Qt::PenStyle style) {
        if (samples.size() < 2) return;
        QPainterPath path;
        for (int index = 0; index < samples.size(); ++index) {
            const double x = area.left() + area.width() * index / (samples.size() - 1);
            const double y = area.bottom() - area.height() * samples[index] / 100.0;
            if (index == 0) path.moveTo(x, y);
            else path.lineTo(x, y);
        }
        painter.setPen(QPen(color, 2, style));
        painter.drawPath(path);
    };
    drawSeries(cpu_, cpuColor, Qt::SolidLine);
    drawSeries(memory_, memoryColor, Qt::DashLine);
}
